#!/usr/bin/env python3
import argparse
import datetime
import hashlib
import json
import os
import socket
import ssl
import threading
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Externally observe an OpenConsole physical UDP/HTTPS probe")
    parser.add_argument("fixture_dir", type=Path)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    config = json.loads((args.fixture_dir / "fixture.json").read_text(encoding="utf-8"))
    evidence_path = args.evidence or args.fixture_dir / "observed-requests.jsonl"
    lock = threading.Lock()

    def record(kind: str, peer, payload: bytes) -> None:
        entry = {
            "schema": 1, "observed_utc": datetime.datetime.now(
                datetime.timezone.utc).isoformat(), "kind": kind,
            "fixture_token": config["token"],
            "peer": peer[0], "peer_port": peer[1], "payload": payload.decode(
                "ascii", errors="replace"), "sha256": hashlib.sha256(payload).hexdigest(),
        }
        with lock, evidence_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry, sort_keys=True) + "\n")
            stream.flush(); os.fsync(stream.fileno())
        print(json.dumps(entry, sort_keys=True), flush=True)

    def udp_server() -> None:
        expected = config["udp_request"].encode("ascii")
        response = config["udp_response"].encode("ascii")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((args.bind, config["udp_port"]))
            while True:
                payload, peer = server.recvfrom(2048)
                record("udp", peer, payload)
                if payload == expected:
                    server.sendto(response, peer)

    thread = threading.Thread(target=udp_server, daemon=True)
    thread.start()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.fixture_dir / "server.pem",
                            args.fixture_dir / "server.key")
    body = config["https_response"].encode("ascii")

    def handle_https(connection: socket.socket, peer) -> None:
        try:
            with context.wrap_socket(connection, server_side=True) as tls:
                request = tls.recv(8192)
                record("https", peer, request)
                tls.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: " +
                            str(len(body)).encode("ascii") +
                            b"\r\nConnection: keep-alive\r\n\r\n" + body)
                time.sleep(5.0)
        except (ssl.SSLError, ConnectionError, TimeoutError) as error:
            record("https-error", peer, str(error).encode("ascii", errors="replace"))

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as raw:
        raw.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        raw.bind((args.bind, config["https_port"]))
        raw.listen(8)
        print(f"Listening on UDP {args.bind}:{config['udp_port']} and "
              f"HTTPS {args.bind}:{config['https_port']}", flush=True)
        try:
            while True:
                connection, peer = raw.accept()
                threading.Thread(target=handle_https, args=(connection, peer),
                                 daemon=True).start()
        except KeyboardInterrupt:
            print("Physical network fixture stopped", flush=True)
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
