#!/usr/bin/env python3
import json
import hashlib
import os
import re
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import threading
import time
import tempfile
import zlib


def wait_until(predicate, description, timeout=30.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {description}")


def log_contains(path, marker):
    try:
        with open(path, "rb") as stream:
            return marker.encode("ascii") in stream.read()
    except FileNotFoundError:
        return False


def log_count(path, marker):
    try:
        with open(path, "rb") as stream:
            return stream.read().count(marker.encode("ascii"))
    except FileNotFoundError:
        return 0


def log_hex_values(path, marker):
    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        return [int(value, 16) for value in re.findall(
            re.escape(marker) + r"([0-9A-F]+)", stream.read())]


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class NetworkFixture:
    def __init__(self):
        self.stop_event = threading.Event()
        self.udp_requests = []
        self.tcp_requests = []
        self.error = None
        self.threads = []

    def start(self):
        for target in (self._serve_udp, self._serve_tcp):
            thread = threading.Thread(target=self._guard, args=(target,), daemon=True)
            thread.start()
            self.threads.append(thread)

    def _guard(self, target):
        try:
            target()
        except Exception as error:
            self.error = error

    def _serve_udp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", 9000))
            server.settimeout(0.1)
            while not self.stop_event.is_set():
                try:
                    payload, peer = server.recvfrom(2048)
                except socket.timeout:
                    continue
                self.udp_requests.append(payload)
                reply = (b"openconsole-bounds-response" if payload == b"openconsole-bounds-probe"
                         else b"openconsole-udp-reply")
                server.sendto(reply, peer)

    def _serve_tcp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("127.0.0.1", 9001))
            server.listen(4)
            server.settimeout(0.1)
            while not self.stop_event.is_set():
                try:
                    connection, _ = server.accept()
                except socket.timeout:
                    continue
                with connection:
                    connection.settimeout(2.0)
                    payload = connection.recv(2048)
                    self.tcp_requests.append(payload)
                    body = b"openconsole-http-reply"
                    connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: " +
                                       str(len(body)).encode("ascii") +
                                       b"\r\nConnection: close\r\n\r\n" + body)

    def close(self):
        self.stop_event.set()
        for thread in self.threads:
            thread.join(timeout=1.0)

    def verify(self):
        if self.error is not None:
            raise RuntimeError(f"network fixture failed: {self.error}")
        if self.udp_requests.count(b"openconsole-udp-probe") != 2:
            raise RuntimeError(f"fixture observed unexpected UDP probe count: {self.udp_requests!r}")
        if self.udp_requests.count(b"openconsole-bounds-probe") != 2:
            raise RuntimeError(f"fixture observed unexpected UDP bounds count: {self.udp_requests!r}")
        expected = b"GET /openconsole HTTP/1.1\r\nHost: fixture.openconsole.test\r\n"
        if len(self.tcp_requests) != 2 or any(not payload.startswith(expected)
                                               for payload in self.tcp_requests):
            raise RuntimeError(f"fixture observed unexpected HTTP requests: {self.tcp_requests!r}")

    def verify_integrated(self):
        if self.error is not None:
            raise RuntimeError(f"network fixture failed: {self.error}")
        if self.udp_requests.count(b"openconsole-integrated-probe") != 2:
            raise RuntimeError(f"fixture observed unexpected integrated probes: {self.udp_requests!r}")

    def verify_physical(self):
        if self.error is not None:
            raise RuntimeError(f"network fixture failed: {self.error}")
        if self.udp_requests != [b"openconsole-udp-probe"]:
            raise RuntimeError(f"fixture observed unexpected physical probes: {self.udp_requests!r}")


class HttpsFixture:
    def __init__(self, directory, mode="identity"):
        self.directory = directory
        self.mode = mode
        self.stop_event = threading.Event()
        self.requests = {9443: [], 9444: [], 9445: []}
        self.error = None
        self.threads = []

    def start(self):
        for port, name in ((9443, "valid"), (9444, "expired"),
                           (9445, "wrong-host")):
            thread = threading.Thread(target=self._guard,
                                      args=(port, name), daemon=True)
            thread.start()
            self.threads.append(thread)

    def _guard(self, port, name):
        try:
            self._serve(port, name)
        except Exception as error:
            if not self.stop_event.is_set():
                self.error = error

    def _serve(self, port, name):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.options |= ssl.OP_NO_TICKET
        context.load_cert_chain(os.path.join(self.directory, name + ".pem"),
                                os.path.join(self.directory, name + ".key"))
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as raw:
            raw.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            raw.bind(("127.0.0.1", port))
            raw.listen(4)
            raw.settimeout(0.1)
            while not self.stop_event.is_set():
                try:
                    connection, _ = raw.accept()
                except socket.timeout:
                    continue
                try:
                    with context.wrap_socket(connection, server_side=True) as tls:
                        print(f"HTTPS fixture {port} completed {tls.version()} handshake",
                              flush=True)
                        tls.settimeout(3.0)
                        request = tls.recv(2048)
                        print(f"HTTPS fixture {port} received {len(request)} HTTP bytes",
                              flush=True)
                        self.requests[port].append(request)
                        if self.mode == "update-e2e":
                            request_line = request.split(b"\r\n", 1)[0]
                            paths = {
                                b"GET /catalog HTTP/1.1": "catalog.occ",
                                b"GET /metadata HTTP/1.1": "metadata.ocum",
                                b"GET /artifact HTTP/1.1": "artifact.efi",
                            }
                            fixture_name = paths.get(request_line)
                            if fixture_name is None:
                                raise RuntimeError(f"unexpected update request: {request_line!r}")
                            with open(os.path.join(os.path.dirname(self.directory),
                                                  "update-fixtures", fixture_name), "rb") as stream:
                                body = stream.read()
                        elif self.mode == "slice":
                            request_line = request.split(b"\r\n", 1)[0]
                            if request_line != b"GET /slice.ocg HTTP/1.1":
                                raise RuntimeError(f"unexpected slice request: {request_line!r}")
                            with open(os.path.join(os.path.dirname(self.directory),
                                                  "slice-fixtures", "slice.ocg"), "rb") as stream:
                                body = stream.read()
                        else:
                            body = b"openconsole-https-valid"
                        # Give the firmware's blocking Response() path time to
                        # post its TCP receive token after Request() completes.
                        self.stop_event.wait(0.5)
                        tls.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: " +
                                    str(len(body)).encode("ascii") +
                                    b"\r\nConnection: keep-alive\r\n\r\n" + body)
                        # HttpDxe's HTTPS response path is blocking. Keep the
                        # connection alive long enough for it to consume the
                        # bounded Content-Length body before any FIN/close alert.
                        self.stop_event.wait(5.0)
                except (ssl.SSLError, ConnectionError, TimeoutError) as error:
                    print(f"HTTPS fixture {port} rejected/closed connection: {error}",
                          flush=True)
                    connection.close()

    def close(self):
        self.stop_event.set()
        for thread in self.threads:
            thread.join(timeout=1.0)

    def verify(self):
        if self.error is not None:
            raise RuntimeError(f"HTTPS fixture failed: {self.error}")
        if len(self.requests[9443]) != 1 or b"GET /update" not in self.requests[9443][0]:
            raise RuntimeError(f"valid HTTPS request missing: {self.requests[9443]!r}")
        if self.requests[9444] or self.requests[9445]:
            raise RuntimeError("rejected TLS identities reached HTTP request handling")

    def verify_update(self):
        if self.error is not None:
            raise RuntimeError(f"HTTPS update fixture failed: {self.error}")
        request_lines = [request.split(b"\r\n", 1)[0]
                         for request in self.requests[9443]]
        expected = [b"GET /catalog HTTP/1.1", b"GET /metadata HTTP/1.1",
                    b"GET /artifact HTTP/1.1"]
        if request_lines != expected:
            raise RuntimeError(f"unexpected update HTTPS sequence: {request_lines!r}")
        if self.requests[9444] or self.requests[9445]:
            raise RuntimeError("rejected update TLS identities reached HTTP handling")

    def verify_slice(self):
        if self.error is not None:
            raise RuntimeError(f"HTTPS slice fixture failed: {self.error}")
        lines = [request.split(b"\r\n", 1)[0] for request in self.requests[9443]]
        if lines != [b"GET /slice.ocg HTTP/1.1"]:
            raise RuntimeError(f"unexpected slice HTTPS sequence: {lines!r}")
        if self.requests[9444] or self.requests[9445]:
            raise RuntimeError("slice download reached rejected TLS endpoints")


class Qmp:
    def __init__(self, path):
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.connection.connect(path)
        self.stream = self.connection.makefile("rwb", buffering=0)
        self._read_reply()
        self.command("qmp_capabilities")

    def _read_reply(self):
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            message = json.loads(line)
            if "event" not in message:
                return message

    def command(self, name, arguments=None):
        request = {"execute": name}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write((json.dumps(request) + "\n").encode("utf-8"))
        reply = self._read_reply()
        if "error" in reply:
            raise RuntimeError(f"QMP {name} failed: {reply['error']}")
        return reply.get("return")

    def hmp(self, command):
        return self.command("human-monitor-command", {"command-line": command})

    def move_mouse(self, x, y):
        # The emulated USB HID report stores each relative axis in a signed
        # byte.  QEMU clamps larger QMP values, so preserve the requested
        # displacement by sending it as a sequence of legal reports.
        while x or y:
            dx = max(-120, min(120, x))
            dy = max(-120, min(120, y))
            events = []
            if dx:
                events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
            if dy:
                events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
            self.command("input-send-event", {"head": 0, "events": events})
            x -= dx
            y -= dy
            time.sleep(0.02)

    def click_left(self):
        self.command("input-send-event", {"head": 0, "events": [
            {"type": "btn", "data": {"button": "left", "down": True}}]})
        time.sleep(0.05)
        self.command("input-send-event", {"head": 0, "events": [
            {"type": "btn", "data": {"button": "left", "down": False}}]})

    def close(self):
        self.stream.close()
        self.connection.close()


class VncPointer:
    def __init__(self, host="127.0.0.1", port=5999):
        self.connection = socket.create_connection((host, port), timeout=5.0)
        version = self._receive(12)
        self.connection.sendall(version)
        count = self._receive(1)[0]
        security = self._receive(count)
        if 1 not in security:
            raise RuntimeError("QEMU VNC does not offer no-auth test security")
        self.connection.sendall(b"\x01")
        if self._receive(4) != b"\x00\x00\x00\x00":
            raise RuntimeError("QEMU VNC security negotiation failed")
        self.connection.sendall(b"\x01")
        header = self._receive(24)
        name_length = int.from_bytes(header[20:24], "big")
        self._receive(name_length)

    def _receive(self, size):
        data = b""
        while len(data) < size:
            chunk = self.connection.recv(size - len(data))
            if not chunk:
                raise RuntimeError("QEMU VNC connection closed")
            data += chunk
        return data

    def pointer(self, x, y, buttons=0):
        self.connection.sendall(bytes((5, buttons)) +
                                int(x).to_bytes(2, "big") +
                                int(y).to_bytes(2, "big"))

    def click(self, x, y):
        self.pointer(x, y, 0)
        self.pointer(x, y, 1)
        self.pointer(x, y, 0)

    def close(self):
        self.connection.close()


class XdotoolPointer:
    def __init__(self):
        result = subprocess.run(
            ["xdotool", "search", "--sync", "--onlyvisible", "--name", "QEMU"],
            check=True, capture_output=True, text=True, timeout=10.0)
        self.window = result.stdout.splitlines()[-1]
        subprocess.run(["xdotool", "windowfocus", "--sync", self.window],
                       check=True)

    def pointer(self, x, y, buttons=0):
        subprocess.run(["xdotool", "mousemove", "--window", self.window,
                        str(x), str(y)], check=True)
        if buttons:
            subprocess.run(["xdotool", "mousedown", "1"], check=True)

    def click(self, x, y):
        subprocess.run(["xdotool", "mousemove", "--window", self.window,
                        str(x), str(y), "click", "1"], check=True)

    def close(self):
        pass


def ppm_to_png(ppm_path, png_path):
    with open(ppm_path, "rb") as stream:
        if stream.readline().strip() != b"P6":
            raise RuntimeError("screenshot is not a binary PPM")
        dimensions = stream.readline().split()
        if len(dimensions) != 2 or stream.readline().strip() != b"255":
            raise RuntimeError("unsupported PPM screenshot header")
        width, height = (int(value) for value in dimensions)
        rgb = stream.read()
    if width < 640 or height < 480 or len(rgb) != width * height * 3:
        raise RuntimeError("screenshot dimensions or payload are invalid")
    raw = b"".join(b"\x00" + rgb[row * width * 3:(row + 1) * width * 3]
                   for row in range(height))

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(png_path, "wb") as stream:
        stream.write(png)


def read_ppm(path):
    with open(path, "rb") as stream:
        if stream.readline().strip() != b"P6":
            raise RuntimeError("scene is not a binary PPM")
        width, height = (int(value) for value in stream.readline().split())
        if stream.readline().strip() != b"255":
            raise RuntimeError("scene has unsupported PPM depth")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise RuntimeError("scene PPM payload is truncated")
    return width, height, pixels


def verify_gles_scene(path):
    """Analytical 9x9 grid diff. Edges are skipped; channel tolerance is 12."""
    width, height, pixels = read_ppm(path)
    background = (10, 20, 31)
    samples = errors = 0
    colorful = 0
    for gy in range(1, 10):
        for gx in range(1, 10):
            x, y = width * gx // 10, height * gy // 10
            actual = tuple(pixels[(y * width + x) * 3 + channel] for channel in range(3))
            nx, ny = x / width * 2.0 - 1.0, 1.0 - y / height * 2.0
            # Triangle vertices (-.75,-.65), (.75,-.65), (0,.75).
            denominator = 2.1
            w0 = ((-.65 - .75) * (nx - 0.0) + (0.0 - .75) * (ny - .75)) / denominator
            w1 = ((.75 - -.65) * (nx - 0.0) + (-.75 - 0.0) * (ny - .75)) / denominator
            w2 = 1.0 - w0 - w1
            if min(abs(w0), abs(w1), abs(w2)) < 0.025:
                continue
            expected = tuple(round(255 * weight) for weight in (w0, w1, w2)) if min(w0, w1, w2) >= 0 else background
            errors += sum(abs(actual[channel] - expected[channel]) for channel in range(3))
            samples += 3
            if max(actual) - min(actual) > 30:
                colorful += 1
    mae = errors / samples
    if mae > 12.0 or colorful < 8:
        raise RuntimeError(f"GLES analytical grid diff failed: MAE={mae:.2f}, colorful={colorful}")
    return mae


def main():
    if len(sys.argv) != 7:
        print("usage: ovmf_interaction_test.py QEMU CODE VARS ESP LOG SCREENSHOT", file=sys.stderr)
        return 2
    qemu, code, variables, esp, log, screenshot = sys.argv[1:]
    gpu_mode = os.environ.get("OPENCONSOLE_TEST_GPU", "ati-probe")
    scene = os.environ.get("OPENCONSOLE_TEST_SCENE", "lifecycle")
    active_boot_path = os.path.join(esp, "EFI", "BOOT", "BOOTX64.EFI")
    active_boot_before = file_sha256(active_boot_path) if scene in ("update-storage", "update-e2e") else None
    requested_accel = os.environ.get("OPENCONSOLE_TEST_ACCEL")
    if requested_accel is None:
        requested_accel = "kvm" if scene in ("audio", "integrated") and os.access("/dev/kvm", os.R_OK | os.W_OK) else "tcg"
    machine = f"{'pc' if scene in ('pointer-pagination', 'game-details') else 'q35'},accel={requested_accel}"
    png_screenshot = os.path.splitext(screenshot)[0] + ".png"
    wav_capture = os.path.join(os.path.dirname(log), "ovmf-audio.wav")
    no_output_metrics = os.path.join(os.path.dirname(log),
                                     "ovmf-audio-no-output-metrics.json")
    physical_inventory_receipt = os.path.join(
        os.path.dirname(log), "ovmf-physical-inventory-media-receipt.json")
    physical_inventory_metrics = os.path.join(
        os.path.dirname(log), "ovmf-physical-inventory-metrics.json")
    progress_screenshot = os.path.splitext(screenshot)[0] + "-progress.ppm"
    filtered_screenshot = os.path.splitext(screenshot)[0] + "-filtered.ppm"
    filtered_png = os.path.splitext(screenshot)[0] + "-filtered.png"
    page_two_screenshot = os.path.splitext(screenshot)[0] + "-page-two.ppm"
    page_two_png = os.path.splitext(screenshot)[0] + "-page-two.png"
    details_screenshot = os.path.splitext(screenshot)[0] + "-modal.ppm"
    details_png = os.path.splitext(screenshot)[0] + "-modal.png"
    confirm_screenshot = os.path.splitext(screenshot)[0] + "-delete-confirm.ppm"
    confirm_png = os.path.splitext(screenshot)[0] + "-delete-confirm.png"
    post_delete_screenshot = os.path.splitext(screenshot)[0] + "-post-delete.ppm"
    post_delete_png = os.path.splitext(screenshot)[0] + "-post-delete.png"
    download_screenshot = os.path.splitext(screenshot)[0] + "-downloading.ppm"
    installed_screenshot = os.path.splitext(screenshot)[0] + "-installed.ppm"
    running_screenshot = os.path.splitext(screenshot)[0] + "-running.ppm"
    returned_screenshot = os.path.splitext(screenshot)[0] + "-returned.ppm"
    # AF_UNIX paths are capped at roughly 108 bytes on Linux. Keep control
    # sockets in a short private directory so clean checkouts in deep paths
    # remain testable.
    qmp_tempdir = tempfile.mkdtemp(prefix="oc-qmp-")
    qmp_path = os.path.join(qmp_tempdir, "qmp.sock")
    reboot_log = log + ".reboot"
    reboot_qmp_path = qmp_path + ".reboot"
    cleanup_paths = (qmp_path, log, screenshot, progress_screenshot, png_screenshot,
                     wav_capture) if scene in ("audio", "integrated", "physical-inventory") else (qmp_path, log, screenshot, png_screenshot)
    if scene in ("dashboard", "pointer-pagination"):
        cleanup_paths += (filtered_screenshot, filtered_png)
    if scene == "pointer-pagination":
        cleanup_paths += (page_two_screenshot, page_two_png)
    if scene == "game-details":
        cleanup_paths += (details_screenshot, details_png, confirm_screenshot,
                          confirm_png, post_delete_screenshot, post_delete_png)
    if scene == "slice":
        cleanup_paths += (download_screenshot, installed_screenshot,
                          running_screenshot, returned_screenshot)
    if scene == "audio-no-output":
        cleanup_paths += (no_output_metrics,)
    elif scene == "physical-inventory":
        cleanup_paths += (physical_inventory_receipt, physical_inventory_metrics)
    if scene == "hang-reset":
        cleanup_paths += (reboot_log, reboot_qmp_path)
    for path in cleanup_paths:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    if gpu_mode == "ati-probe":
        pointer_display = os.environ.get("OPENCONSOLE_POINTER_DISPLAY", "vnc=127.0.0.1:99")
        display = pointer_display if scene in ("pointer-pagination", "game-details", "integrated") else "none"
        gpu_arguments = (["-vga", "std"] if scene in ("pointer-pagination", "game-details", "integrated", "slice") else
                         ["-device", "VGA", "-device", "ati-vga,model=rv100,addr=03.0"])
    elif gpu_mode == "virtio-gl":
        display = "egl-headless"
        gpu_arguments = ["-vga", "none", "-device", "virtio-vga-gl"]
    else:
        raise RuntimeError(f"unknown OPENCONSOLE_TEST_GPU mode: {gpu_mode}")
    command = [
        qemu,
        "-machine", machine,
        "-m", "256M",
        "-no-reboot",
        "-display", display,
        "-serial", "none",
        "-monitor", "none",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={variables}",
        "-drive", (f"format=raw,file={esp}" if os.path.isfile(esp)
                   else f"format=raw,file=fat:rw:{esp}"),
        "-debugcon", f"file:{log}",
        "-global", "isa-debugcon.iobase=0x402",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
    ] + gpu_arguments
    if scene in ("pointer-pagination", "game-details", "integrated", "slice"):
        command += ["-usb", "-device", "usb-mouse,id=ocmouse"]
    if os.environ.get("OPENCONSOLE_TEST_PACKAGED_NETWORK") == "1":
        command += ["-cpu", "max", "-object",
                    "rng-random,filename=/dev/urandom,id=ocrng",
                    "-device", "virtio-rng-pci,rng=ocrng"]
    if scene in ("audio", "integrated", "slice", "physical-inventory"):
        command += ["-audiodev", f"wav,id=ocwav,path={wav_capture},out.frequency=48000,out.channels=2,out.format=s16",
                    "-device", "ich9-intel-hda",
                    "-device", "hda-output,audiodev=ocwav"]
    elif scene == "audio-no-output":
        # A controller with no codec forces AudioDxe's post-start/no-AudioIo
        # cleanup path without manufacturing a playable backend.
        command += ["-device", "ich9-intel-hda"]
    fixture = NetworkFixture() if scene in ("network-fixture", "integrated", "slice", "physical-network", "game-details") else None
    https_fixture = (HttpsFixture(os.path.join(os.path.dirname(log), "tls-fixtures"), scene)
                     if scene in ("https", "update-e2e", "physical-network", "slice") else None)
    if https_fixture is not None:
        https_fixture.start()
    if fixture is not None:
        fixture.start()
    if fixture is not None or https_fixture is not None:
        command += ["-nic", "none", "-netdev",
                    "user,id=ocnet,net=10.0.2.0/24,dhcpstart=10.0.2.15",
                    "-device", "virtio-net-pci,netdev=ocnet"]
    elif os.environ.get("OPENCONSOLE_TEST_NETWORK") == "none":
        command += ["-nic", "none"]
    process = subprocess.Popen(command)
    qmp = None
    vnc_pointer = None
    child_elapsed = []
    try:
        wait_until(lambda: os.path.exists(qmp_path), "QMP socket")
        qmp = Qmp(qmp_path)
        if scene == "integrated" and display.startswith("vnc=127.0.0.1:"):
            vnc_display = int(display.rsplit(":", 1)[1])
            vnc_pointer = VncPointer(port=5900 + vnc_display)
        elif scene == "integrated" and display == "gtk":
            vnc_pointer = XdotoolPointer()
        wait_until(lambda: log_contains(log, "OPENCONSOLE_MENU_READY"), "parent menu")
        qmp.command("screendump", {"filename": screenshot})
        if scene == "catalog-acceptance":
            required_before = (
                "OPENCONSOLE_GAME_TRANSACTION_VERIFIED_ID_analyzer-fixture",
                "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001",
            )
            missing = [marker for marker in required_before if not log_contains(log, marker)]
            if missing:
                raise RuntimeError(f"catalog-installed game failed verification: {missing}")
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_STARTING"),
                       "catalog child launch")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "catalog child return")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_LAUNCH_COMPLETION_PERSISTED"),
                       "catalog child completion journal")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_LIBRARY_RESCANNED_AFTER_CHILD"),
                       "catalog child library rescan")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("catalog acceptance QEMU did not quit cleanly")
            ppm_to_png(screenshot, png_screenshot)
            print("PASS: hosted catalog bundle verified, launched, returned, and completed its journal")
            print(f"SCREENSHOT: {png_screenshot}")
            return 0
        if scene == "mutation-after-discovery":
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_TEST_GAME_MUTATED_AFTER_DISCOVERY"),
                "post-discovery game mutation")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_INTEGRITY_GAME_REOPEN_REJECTED"),
                "mutated game rejection")
            if log_contains(log, "OPENCONSOLE_CHILD_STARTING"):
                raise RuntimeError("mutated game bytes reached LoadImage")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("mutation-after-discovery QEMU did not quit cleanly")
            print("PASS: launch-time authenticated reopen rejected post-discovery mutation")
            return 0
        if scene == "unsupported-watchdog":
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_WATCHDOG_UNSUPPORTED"),
                       "unsupported watchdog diagnostic")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"),
                       "child under unsupported-watchdog firmware")
            qmp.hmp("sendkey x")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_LAUNCH_COMPLETION_PERSISTED"),
                "completion journal without watchdog")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "child return without watchdog")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("unsupported-watchdog QEMU did not quit cleanly")
            print("PASS: unsupported firmware watchdog remained diagnosable and recoverable")
            return 0
        if scene == "corrupt-driver":
            required = (
                "OPENCONSOLE_INTEGRITY_PLATFORM_IMAGE_REJECTED",
                "OPENCONSOLE_AUDIO_OFFLINE_NO_AUDIOIO",
                "OPENCONSOLE_MENU_READY",
            )
            missing = [marker for marker in required if not log_contains(log, marker)]
            if missing:
                raise RuntimeError(f"corrupt optional driver did not fail closed: {missing}")
            if log_contains(log, "OPENCONSOLE_AUDIO_DRIVER_STARTED"):
                raise RuntimeError("corrupt optional driver was started")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("corrupt-driver QEMU did not quit cleanly")
            print("PASS: corrupt inventoried optional driver was rejected with baseline UI intact")
            return 0
        if scene == "hang-reset":
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_LAUNCH_ATTEMPT_PERSISTED"),
                       "persisted launch attempt before hang")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_HANG_CHILD_READY"),
                       "non-returning child")
            # A firmware watchdog ultimately produces a platform reset, not a
            # cooperative return from StartImage. Stop QEMU without giving the
            # child or parent another instruction, then cold-boot the exact
            # same writable FAT image. The attempt record was flushed before
            # StartImage and must survive this process boundary.
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("hung-child QEMU did not stop cleanly")
            qmp.close()
            qmp = None
            restart_command = list(command)
            restart_command[restart_command.index(f"file:{log}")] = f"file:{reboot_log}"
            restart_command[restart_command.index(
                f"unix:{qmp_path},server=on,wait=off")] = (
                    f"unix:{reboot_qmp_path},server=on,wait=off")
            process = subprocess.Popen(restart_command)
            wait_until(lambda: os.path.exists(reboot_qmp_path), "reboot QMP socket")
            qmp = Qmp(reboot_qmp_path)
            wait_until(lambda: log_contains(reboot_log, "OPENCONSOLE_PARENT_BOOTED"),
                       "parent cold boot after forced reset", timeout=45.0)
            wait_until(lambda: log_contains(reboot_log, "OPENCONSOLE_MENU_READY"),
                       "safe-mode selector after forced reset", timeout=45.0)
            required = (
                "OPENCONSOLE_RECOVERY_GAME_QUARANTINED",
                "OPENCONSOLE_SAFE_MODE_OPTIONAL_DRIVERS_SUPPRESSED",
                "OPENCONSOLE_GAME_VISIBLE_QUARANTINED_ID_hang_probe",
            )
            missing = [marker for marker in required
                       if not log_contains(reboot_log, marker)]
            if missing:
                raise RuntimeError(f"forced-reset quarantine evidence missing: {missing}")
            if log_contains(reboot_log, "OPENCONSOLE_AUDIO_DRIVER_STARTED"):
                raise RuntimeError("safe mode started an optional audio driver")
            qmp.command("screendump", {"filename": screenshot})
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("hang-reset QEMU did not quit cleanly")
            print("PASS: non-returning child plus reset quarantined the exact bundle on next boot")
            return 0
        if scene == "dashboard":
            for character in "second":
                qmp.hmp(f"sendkey {character}")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_QUERY_second"),
                "dashboard query")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001"),
                "single filtered dashboard result")
            qmp.command("screendump", {"filename": filtered_screenshot})
        if scene == "pointer-pagination":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_POINTER_READY"),
                       "UEFI Simple Pointer discovery")
            mice = qmp.hmp("info mice")
            usb_mouse = next((line for line in mice.splitlines()
                              if "HID Mouse" in line or "USB Mouse" in line or
                              "Virtio Mouse" in line), None)
            if usb_mouse is None:
                raise RuntimeError(f"QEMU USB mouse was not registered: {mice}")
            mouse_number = usb_mouse.split("Mouse #", 1)[1].split(":", 1)[0]
            qmp.hmp(f"mouse_set {mouse_number}")
            qmp.move_mouse(1116, -396)
            qmp.click_left()
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_POINTER_PAGE_CHANGED_0000000000000002"),
                "mouse next-page activation")
            # The serial marker is emitted before the new GOP frame has fully
            # blitted. Avoid capturing a deliberately torn transition frame.
            time.sleep(0.25)
            qmp.command("screendump", {"filename": page_two_screenshot})
            for character in "09":
                qmp.hmp(f"sendkey {character}")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_QUERY_09"), "live pointer-scene filter")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001"),
                "filtered pointer-scene result")
            qmp.command("screendump", {"filename": filtered_screenshot})
            qmp.move_mouse(-1116, 282)
            qmp.click_left()
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_LAUNCH_ID_library_09"),
                "filtered card mouse launch")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"),
                       "pointer-launched child EFI")
        if scene == "game-details":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CONNECTIVITY_ONLINE"),
                       "provider-backed online status", timeout=45.0)
            wait_until(lambda: log_contains(log, "OPENCONSOLE_POINTER_READY"),
                       "UEFI USB pointer")
            mice = qmp.hmp("info mice")
            usb_mouse = next((line for line in mice.splitlines()
                              if "HID Mouse" in line or "USB Mouse" in line or
                              "Virtio Mouse" in line), None)
            if usb_mouse is None:
                raise RuntimeError(f"QEMU USB mouse was not registered: {mice}")
            mouse_number = usb_mouse.split("Mouse #", 1)[1].split(":", 1)[0]
            qmp.hmp(f"mouse_set {mouse_number}")
            qmp.move_mouse(-494, 12)
            qmp.click_left()
            wait_until(lambda: log_contains(log, "OPENCONSOLE_DETAILS_OPEN_ID_library_00"),
                       "details modal identity")
            time.sleep(0.25)
            qmp.command("screendump", {"filename": details_screenshot})
            qmp.move_mouse(1516, -524)
            qmp.click_left()
            for character in "00":
                qmp.hmp(f"sendkey {character}")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_DASHBOARD_QUERY_00"),
                       "filtered details query")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001"),
                "single filtered details result")
            qmp.move_mouse(-908, 524)
            qmp.click_left()
            wait_until(lambda: log_count(log, "OPENCONSOLE_DETAILS_OPEN_ID_library_00") >= 2,
                       "filtered details reopen identity")
            qmp.move_mouse(466, 574)
            qmp.click_left()
            wait_until(lambda: log_contains(log, "OPENCONSOLE_DASHBOARD_LAUNCH_ID_library_00"),
                       "modal exact launch")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"),
                       "modal-launched child")
            qmp.hmp("sendkey x")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "modal child return")
            qmp.hmp("sendkey esc")
            time.sleep(0.25)
            qmp.hmp("sendkey esc")
            wait_until(lambda: log_count(log, "OPENCONSOLE_DASHBOARD_QUERY_EMPTY") >= 2,
                       "query restored before deletion")
            qmp.move_mouse(-1074, -574)
            qmp.click_left()
            wait_until(lambda: log_count(log, "OPENCONSOLE_DETAILS_OPEN_ID_library_00") >= 3,
                       "delete target details identity")
            qmp.move_mouse(1354, 574)
            qmp.click_left()
            time.sleep(0.25)
            qmp.command("screendump", {"filename": confirm_screenshot})
            qmp.move_mouse(-700, -440)
            qmp.click_left()
            wait_until(lambda: log_contains(log, "OPENCONSOLE_DELETE_SUCCESS_ID_library_00"),
                       "confined package deletion")
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_DASHBOARD_RESULTS_000000000000000B"),
                "post-delete authoritative rescan")
            time.sleep(0.25)
            qmp.command("screendump", {"filename": post_delete_screenshot})
            # QEMU's writable vvfat backend commits directory mutations while
            # the guest drive is torn down, so verify the backing tree only
            # after a clean VM exit. The in-guest rescan above proves that the
            # firmware itself observed the eleven-package state first.
            qmp.command("quit")
            return_code = process.wait(timeout=10.0)
            if return_code != 0:
                raise RuntimeError(f"unexpected QEMU quit status {return_code}")
            # QEMU vvfat does not propagate guest directory deletion back to
            # its source directory. Exact removal is therefore asserted by
            # the firmware's successful operation plus its authoritative
            # eleven-game rescan, rather than by the unchanged host fixture.
            if not os.path.isfile(os.path.join(esp, "games", "library_01", "game.toml")):
                raise RuntimeError("unrelated game package was changed")
            if not os.path.isfile(os.path.join(esp, "games", "malicious_target",
                                               "do-not-delete.txt")):
                raise RuntimeError("rejected malicious package was changed")
            fixture.close()
            fixture = None
            for source, target in ((screenshot, png_screenshot),
                                   (details_screenshot, details_png),
                                   (confirm_screenshot, confirm_png),
                                   (post_delete_screenshot, post_delete_png)):
                ppm_to_png(source, target)
            print("PASS: online two-row library, details launch, child return, and confined delete")
            print(f"LIBRARY SCREENSHOT: {png_screenshot}")
            print(f"DETAILS SCREENSHOT: {details_png}")
            print(f"DELETE CONFIRM SCREENSHOT: {confirm_png}")
            print(f"POST DELETE SCREENSHOT: {post_delete_png}")
            return 0
        if scene == "physical-inventory":
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_PHYSICAL_INVENTORIES_CAPTURED"),
                "physical-test inventory capture", timeout=60.0)
            if log_count(log, "OPENCONSOLE_IO_INVENTORY_LOG_OPENED") != 2 or log_count(
                    log, "OPENCONSOLE_IO_INVENTORY_LOG_FLUSHED") != 2:
                raise RuntimeError("both inventory logs were not opened and flushed exactly once")
            qmp.command("quit")
            return_code = process.wait(timeout=10.0)
            if return_code != 0:
                raise RuntimeError(f"unexpected QEMU quit status {return_code}")
            verifier = os.path.join(os.path.dirname(__file__),
                                    "verify-physical-test-media.py")
            subprocess.run([sys.executable, verifier, esp, "--collected",
                            "--output", physical_inventory_receipt], check=True)
            with open(physical_inventory_receipt, "r", encoding="utf-8") as stream:
                receipt = json.load(stream)
            if receipt.get("mode") != "collected" or not receipt.get(
                    "inventory_stages_complete"):
                raise RuntimeError("physical media collection receipt is incomplete")
            with open(os.path.join(esp, "OPENCONSOLE-INVENTORY.LOG"), "rb") as stream:
                firmware_inventory = stream.read()
            with open(os.path.join(esp, "OPENCONSOLE-DRIVER-INVENTORY.LOG"), "rb") as stream:
                post_driver_inventory = stream.read()
            if b"OPENCONSOLE_IO_AUDIO_DRIVER_DOWNLOAD_OPENCORE_AUDIODXE\n" not in firmware_inventory:
                raise RuntimeError("firmware inventory did not recommend AudioDxe for HDA")
            if b"OPENCONSOLE_IO_AUDIO_DRIVER_USE_EXISTING_AUDIOIO\n" not in post_driver_inventory:
                raise RuntimeError("post-driver inventory did not select installed AudioIo")
            ppm_to_png(screenshot, png_screenshot)
            metrics = {
                "schema": "openconsole.ovmf-physical-inventory.v1",
                "scope": "ovmf-packaged-physical-test-media-persistence",
                "physical_hardware_evidence": False,
                "inventory_open_count": log_count(
                    log, "OPENCONSOLE_IO_INVENTORY_LOG_OPENED"),
                "inventory_flush_count": log_count(
                    log, "OPENCONSOLE_IO_INVENTORY_LOG_FLUSHED"),
                "qemu_exit_status": return_code,
                "debug_log_sha256": file_sha256(log),
                "firmware_inventory_sha256": file_sha256(os.path.join(
                    esp, "OPENCONSOLE-INVENTORY.LOG")),
                "post_driver_inventory_sha256": file_sha256(os.path.join(
                    esp, "OPENCONSOLE-DRIVER-INVENTORY.LOG")),
                "media_receipt_sha256": file_sha256(physical_inventory_receipt),
                "screenshot_sha256": file_sha256(png_screenshot),
            }
            with open(physical_inventory_metrics, "w", encoding="utf-8") as stream:
                json.dump(metrics, stream, indent=2, sort_keys=True)
                stream.write("\n")
            print("PASS: packaged physical-test EFI persisted and verified both OVMF inventories")
            print(f"METRICS: {physical_inventory_metrics}")
            print(f"RECEIPT: {physical_inventory_receipt}")
            print(f"SCREENSHOT: {png_screenshot}")
            return 0
        if scene == "audio-no-output":
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_AUDIO_DRIVER_NO_OUTPUT_CLEANUP"),
                "AudioDxe no-output cleanup", timeout=45.0)
            wait_until(lambda: log_contains(
                log, "OPENCONSOLE_AUDIO_OFFLINE_NO_AUDIOIO"),
                "offline audio fallback", timeout=15.0)
            if log_contains(log, "OPENCONSOLE_AUDIO_PROVIDER_READY"):
                raise RuntimeError("audio provider was published without an AudioIo output")
            if log_count(log, "OPENCONSOLE_AUDIO_DRIVER_STARTED") != 1 or log_count(
                    log, "OPENCONSOLE_AUDIO_DRIVER_NO_OUTPUT_CLEANUP") != 1:
                raise RuntimeError("AudioDxe no-output lifecycle was not singular and deterministic")
            if not os.path.isfile(screenshot) or os.path.getsize(screenshot) < 1024:
                raise RuntimeError("offline-audio GOP screenshot was not captured")
            qmp.command("quit")
            return_code = process.wait(timeout=10.0)
            if return_code != 0:
                raise RuntimeError(f"unexpected QEMU quit status {return_code}")
            ppm_to_png(screenshot, png_screenshot)
            metrics = {
                "schema": "openconsole.audio-no-output-evidence.v1",
                "scope": "ovmf-qemu-simulated-controller-without-codec",
                "controller_present": True,
                "codec_present": False,
                "driver_start_count": log_count(log, "OPENCONSOLE_AUDIO_DRIVER_STARTED"),
                "cleanup_count": log_count(
                    log, "OPENCONSOLE_AUDIO_DRIVER_NO_OUTPUT_CLEANUP"),
                "provider_published": log_contains(
                    log, "OPENCONSOLE_AUDIO_PROVIDER_READY"),
                "offline_fallback_observed": log_contains(
                    log, "OPENCONSOLE_AUDIO_OFFLINE_NO_AUDIOIO"),
                "parent_menu_rendered": True,
                "qemu_exit_status": return_code,
                "log_sha256": file_sha256(log),
                "screenshot_sha256": file_sha256(png_screenshot),
            }
            with open(no_output_metrics, "w", encoding="utf-8") as stream:
                json.dump(metrics, stream, indent=2, sort_keys=True)
                stream.write("\n")
            print("PASS: AudioDxe no-output cleanup preserved the offline parent menu")
            print(f"METRICS: {no_output_metrics}")
            print(f"SCREENSHOT: {png_screenshot}")
            return 0
        if scene == "slice":
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_DOWNLOAD_BEGIN"),
                       "slice download start", timeout=60.0)
            qmp.command("screendump", {"filename": download_screenshot})
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_INSTALL_COMPLETE") or
                       log_contains(log, "OPENCONSOLE_SLICE_INSTALL_FAILED_STATUS_"),
                       "slice package installation", timeout=300.0)
            if log_contains(log, "OPENCONSOLE_SLICE_INSTALL_FAILED_STATUS_"):
                raise RuntimeError("slice installer reported failure")
            qmp.command("screendump", {"filename": installed_screenshot})
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_count(log, "OPENCONSOLE_CHILD_RETURNED") >= 1 and
                       log_contains(log, "OPENCONSOLE_LIBRARY_RESCANNED_AFTER_CHILD") and
                       log_contains(log, "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000002"),
                       "installed game library rescan", timeout=30.0)
            time.sleep(0.25)
            qmp.command("screendump", {"filename": installed_screenshot})
            qmp.hmp("sendkey right")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SELECTION_CHANGED"),
                       "downloaded game selection")
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_INTEGRATED_CHILD_READY"),
                       "downloaded slice launch", timeout=60.0)
            run_started = time.monotonic()
            mice = qmp.hmp("info mice")
            game_mouse = next((line for line in mice.splitlines()
                               if "HID Mouse" in line or "USB Mouse" in line), None)
            if game_mouse is None:
                raise RuntimeError(f"QEMU game mouse was not registered: {mice}")
            mouse_number = game_mouse.split("Mouse #", 1)[1].split(":", 1)[0]
            qmp.hmp(f"mouse_set {mouse_number}")
            qmp.move_mouse(96, 0)
            qmp.click_left()
            for key in ("j", "l", "k"):
                qmp.hmp(f"sendkey {key}")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_KEY_LEFT") and
                       log_contains(log, "OPENCONSOLE_SLICE_KEY_RIGHT") and
                       log_contains(log, "OPENCONSOLE_INTEGRATED_EFFECT_TRIGGERED") and
                       log_contains(log, "OPENCONSOLE_SLICE_MOUSE_MOVED") and
                       log_contains(log, "OPENCONSOLE_SLICE_MOUSE_SOUND") and
                       log_contains(log, "OPENCONSOLE_INTEGRATED_NETWORK_OK"),
                       "slice mouse, keyboard, audio, and network", timeout=60.0)
            time.sleep(2.0)
            qmp.command("screendump", {"filename": running_screenshot})
            qmp.hmp("sendkey q")
            wait_until(lambda: log_count(log, "OPENCONSOLE_CHILD_RETURNED") >= 2,
                       "slice return to parent", timeout=30.0)
            run_elapsed = time.monotonic() - run_started
            time.sleep(0.25)
            qmp.command("screendump", {"filename": returned_screenshot})
            qmp.command("quit")
            return_code = process.wait(timeout=10.0)
            if return_code != 0:
                raise RuntimeError(f"unexpected QEMU quit status {return_code}")
            https_fixture.verify_slice()
            if fixture.error is not None or fixture.udp_requests.count(
                    b"openconsole-integrated-probe") != 1:
                raise RuntimeError(f"slice network probe mismatch: {fixture.udp_requests!r}")
            frames = log_hex_values(log, "OPENCONSOLE_INTEGRATED_FRAME_COUNT_")
            total_cycles = log_hex_values(log, "OPENCONSOLE_SLICE_RENDER_CYCLES_TOTAL_")
            max_cycles = log_hex_values(log, "OPENCONSOLE_SLICE_RENDER_CYCLES_MAX_")
            if len(frames) != 1 or len(total_cycles) != 1 or len(max_cycles) != 1:
                raise RuntimeError("slice render telemetry is incomplete")
            rendered_fps = frames[0] / run_elapsed
            shots = [screenshot, download_screenshot, installed_screenshot,
                     running_screenshot, returned_screenshot]
            pngs = [os.path.splitext(path)[0] + ".png" for path in shots]
            for source, target in zip(shots, pngs):
                with open(source, "rb") as stream:
                    if stream.readline().strip() != b"P6" or stream.readline().strip() != b"1920 1080":
                        raise RuntimeError(f"screenshot is not 1920x1080: {source}")
                ppm_to_png(source, target)
            metrics_path = os.path.join(os.path.dirname(log), "ovmf-slice-metrics.json")
            metrics = {
                "schema": "openconsole.slice-e2e.v1",
                "downloaded_bytes": os.path.getsize(os.path.join(
                    os.path.dirname(log), "slice-fixtures", "slice.ocg")),
                "resolution": [1920, 1080],
                "target_fps": 60,
                "rendered_frames": frames[0],
                "observed_seconds": run_elapsed,
                "observed_fps": rendered_fps,
                "fps_target_met": 50.0 <= rendered_fps <= 70.0,
                "render_cycles_total": total_cycles[0],
                "render_cycles_average": total_cycles[0] / frames[0],
                "render_cycles_max": max_cycles[0],
                "keyboard_mapping_verified": True,
                "placeholder_audio_verified": True,
                "mouse_protocol_discovered": log_contains(log, "OPENCONSOLE_SLICE_POINTER_READY"),
                "mouse_event_verified": (log_contains(log, "OPENCONSOLE_SLICE_MOUSE_MOVED") and
                                           log_contains(log, "OPENCONSOLE_SLICE_MOUSE_SOUND")),
                "returned_to_parent": True,
            }
            with open(metrics_path, "w", encoding="utf-8") as stream:
                json.dump(metrics, stream, indent=2, sort_keys=True)
                stream.write("\n")
            fixture.close(); fixture = None
            https_fixture.close(); https_fixture = None
            print("PASS: downloaded, installed, launched, measured, and returned from GLES2 slice")
            print(f"METRICS: {metrics_path}")
            for path in pngs:
                print(f"SCREENSHOT: {path}")
            return 0
        if scene == "recovery":
            required_before = (
                "OPENCONSOLE_RECOVERY_GAME_QUARANTINED",
                "OPENCONSOLE_SAFE_MODE_OPTIONAL_DRIVERS_SUPPRESSED",
                "OPENCONSOLE_GAME_VISIBLE_QUARANTINED_ID_authenticated",
                "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001",
                "OPENCONSOLE_MENU_READY",
            )
            missing = [marker for marker in required_before if not log_contains(log, marker)]
            if missing:
                raise RuntimeError(f"missing safe-mode markers: {missing}")
            qmp.command("screendump", {"filename": screenshot})
            qmp.hmp("sendkey r")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_RECOVERY_MANUAL_RETRY_ENABLED"),
                       "manual retry enabled")
            wait_until(lambda: log_count(log, "OPENCONSOLE_GAME_TRANSACTION_VERIFIED_ID_authenticated") >= 2,
                       "quarantined game restored")
            qmp.hmp("sendkey ret")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"), "retried child")
            qmp.hmp("sendkey x")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "retried child return")
            required_after = (
                "OPENCONSOLE_LAUNCH_ATTEMPT_PERSISTED",
                "OPENCONSOLE_WATCHDOG_ARMED",
                "OPENCONSOLE_LAUNCH_COMPLETION_PERSISTED",
                "OPENCONSOLE_LIBRARY_RESCANNED_AFTER_CHILD",
            )
            missing = [marker for marker in required_after if not log_contains(log, marker)]
            if missing:
                raise RuntimeError(f"missing retry lifecycle markers: {missing}")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError("recovery QEMU did not quit cleanly")
            ppm_to_png(screenshot, png_screenshot)
            print("PASS: incomplete launch entered baseline safe mode, quarantined exact bundle, and completed explicit retry")
            print(f"SCREENSHOT: {png_screenshot}")
            return 0
        if scene == "storage-recovery":
            case_name = os.environ["OPENCONSOLE_STORAGE_CASE"]
            expected_count = 0 if case_name == "delete-tombstone" else 1
            count_marker = f"OPENCONSOLE_DASHBOARD_RESULTS_{expected_count:016X}"
            wait_until(lambda: log_contains(log, count_marker),
                       f"storage recovery count for {case_name}")
            if case_name == "delete-tombstone":
                required = ("OPENCONSOLE_GAME_RECOVERY_GENERATION_0000000000000002",
                            "OPENCONSOLE_GAME_RECOVERY_TOMBSTONE_ID_authenticated")
            elif case_name == "selector-commit":
                required = ("OPENCONSOLE_GAME_RECOVERY_GENERATION_0000000000000002",
                            "OPENCONSOLE_GAME_TRANSACTION_VERIFIED_ID_authenticated")
            else:
                required = ("OPENCONSOLE_GAME_RECOVERY_GENERATION_0000000000000001",
                            "OPENCONSOLE_GAME_TRANSACTION_VERIFIED_ID_authenticated")
            missing = [marker for marker in required if not log_contains(log, marker)]
            if missing:
                raise RuntimeError(f"missing storage recovery markers for {case_name}: {missing}")
            cleanup_counts = log_hex_values(
                log, "OPENCONSOLE_GAME_RECOVERY_CLEANUP_COUNT_")
            if len(cleanup_counts) != 1:
                raise RuntimeError(
                    f"missing or duplicate cleanup evidence for {case_name}: {cleanup_counts}")
            if case_name not in ("selector-commit", "delete-tombstone") and cleanup_counts[0] == 0:
                raise RuntimeError(f"interrupted state was not cleaned for {case_name}")
            if expected_count:
                qmp.hmp("sendkey ret")
                wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"),
                           f"recovered child for {case_name}")
                qmp.hmp("sendkey x")
                wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                           f"recovered child return for {case_name}")
            qmp.command("quit")
            if process.wait(timeout=10.0) != 0:
                raise RuntimeError(f"storage recovery QEMU did not quit cleanly: {case_name}")
            print(f"PASS: OVMF recovered persistent game transaction state {case_name}")
            return 0
        if scene not in ("gles", "network-fixture", "audio", "integrated", "dashboard", "pointer-pagination"):
            qmp.hmp("sendkey right")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SELECTION_CHANGED"), "parent selection")
        if scene != "pointer-pagination":
            qmp.hmp("sendkey ret")
        if scene == "gles":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_GLES_SCENE_READY"), "GLES scene")
            qmp.command("screendump", {"filename": screenshot})
            image_mae = verify_gles_scene(screenshot)
        elif scene == "audio":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_AUDIO_CHILD_READY"),
                       "audio child", timeout=45.0)
        elif scene == "integrated":
            wait_until(lambda: log_count(log, "OPENCONSOLE_INTEGRATED_CHILD_READY") >= 1,
                       "integrated child", timeout=45.0)
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_POINTER_READY"),
                       "integrated Simple Pointer", timeout=15.0)
            mice = qmp.hmp("info mice")
            usb_mouse = next((line for line in mice.splitlines()
                              if "HID Mouse" in line or "USB Mouse" in line or
                              "Virtio Mouse" in line), None)
            if usb_mouse is None:
                raise RuntimeError(f"QEMU USB mouse was not registered: {mice}")
            mouse_number = usb_mouse.split("Mouse #", 1)[1].split(":", 1)[0]
            qmp.hmp(f"mouse_set {mouse_number}")
            qmp.hmp("sendkey j")
            qmp.hmp("sendkey l")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_KEY_LEFT") and
                       log_contains(log, "OPENCONSOLE_SLICE_KEY_RIGHT"),
                       "customized keyboard movement", timeout=15.0)
        elif scene == "pointer-pagination":
            pass
        elif scene not in ("network-fixture", "physical-network", "crypto", "update-storage", "update-e2e", "https"):
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_READY"), "child EFI")
        if scene == "network-fixture":
            child_started = time.monotonic()
            wait_until(lambda: log_count(log, "OPENCONSOLE_CHILD_RETURNED") >= 1,
                       "first network child return", timeout=45.0)
            child_elapsed.append(time.monotonic() - child_started)
            qmp.hmp("sendkey ret")
            child_started = time.monotonic()
            wait_until(lambda: log_count(log, "OPENCONSOLE_CHILD_RETURNED") >= 2,
                       "second network child return", timeout=45.0)
            child_elapsed.append(time.monotonic() - child_started)
        elif scene == "physical-network":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "physical network child return", timeout=120.0)
        elif scene == "audio":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "audio child return", timeout=60.0)
        elif scene == "crypto":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "crypto child return", timeout=60.0)
        elif scene == "update-storage":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "update storage child return", timeout=60.0)
        elif scene == "update-e2e":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "end-to-end update child return", timeout=300.0)
        elif scene == "https":
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "HTTPS child return", timeout=90.0)
        elif scene == "integrated":
            for launch in (1, 2):
                frame_baseline = log_count(log, "OPENCONSOLE_INTEGRATED_FRAME")
                if launch == 1:
                    if vnc_pointer is not None:
                        vnc_pointer.pointer(640, 400)
                        vnc_pointer.click(760, 400)
                    else:
                        # The legacy HMP mouse commands target the device
                        # chosen by mouse_set.
                        qmp.hmp("mouse_move 96 0")
                        qmp.hmp("mouse_button 1")
                        time.sleep(0.05)
                        qmp.hmp("mouse_button 0")
                    wait_until(lambda: log_contains(log, "OPENCONSOLE_SLICE_MOUSE_MOVED") and
                               log_contains(log, "OPENCONSOLE_SLICE_MOUSE_SOUND"),
                               "mouse movement and sound", timeout=15.0)
                else:
                    qmp.hmp("sendkey k")
                wait_until(lambda launch=launch: log_count(
                    log, ("OPENCONSOLE_SLICE_MOUSE_SOUND" if launch == 1 else
                          "OPENCONSOLE_INTEGRATED_EFFECT_TRIGGERED")) >= 1,
                    f"integrated effect {launch}", timeout=15.0)
                wait_until(lambda launch=launch: log_count(
                    log, "OPENCONSOLE_INTEGRATED_NETWORK_OK") >= launch,
                    f"integrated network {launch}", timeout=45.0)
                wait_until(lambda: log_count(
                    log, "OPENCONSOLE_INTEGRATED_FRAME") > frame_baseline,
                    f"integrated graphics {launch}", timeout=15.0)
                if launch == 1:
                    qmp.command("screendump", {"filename": screenshot})
                    first_frame_count = log_count(log, "OPENCONSOLE_INTEGRATED_FRAME")
                    wait_until(lambda: log_count(
                        log, "OPENCONSOLE_INTEGRATED_FRAME") > first_frame_count,
                        "integrated visual progress", timeout=15.0)
                    qmp.command("screendump", {"filename": progress_screenshot})
                    if file_sha256(screenshot) == file_sha256(progress_screenshot):
                        raise RuntimeError("integrated GLES frames did not change visually")
                qmp.hmp("sendkey q" if launch == 1 else "sendkey e")
                return_marker = ("OPENCONSOLE_CHILD_RETURNED" if launch == 1
                                 else "OPENCONSOLE_CHILD_ERROR_RETURNED")
                wait_until(lambda return_marker=return_marker: log_contains(log, return_marker),
                           f"integrated child return {launch}", timeout=15.0)
                wait_until(lambda launch=launch: log_count(
                    log, "OPENCONSOLE_AUDIO_PARENT_CLEAN") >= launch and log_count(
                    log, "OPENCONSOLE_NET_GENERATION_CLEAN") >= launch,
                    f"integrated cleanup {launch}", timeout=15.0)
                if launch == 1:
                    qmp.hmp("sendkey ret")
                    wait_until(lambda: log_count(
                        log, "OPENCONSOLE_INTEGRATED_CHILD_READY") >= 2,
                        "second integrated child", timeout=45.0)
        elif scene == "pointer-pagination":
            qmp.hmp("sendkey x")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"),
                       "pointer child return")
        else:
            qmp.hmp("sendkey x")
            wait_until(lambda: log_contains(log, "OPENCONSOLE_CHILD_RETURNED"), "parent resume")
        return_code = process.wait(timeout=10.0)
        if return_code != 33:
            raise RuntimeError(f"unexpected QEMU exit status {return_code}, expected 33")
        required = [
            "OPENCONSOLE_PARENT_BOOTED",
            "OPENCONSOLE_MENU_READY",
            "OPENCONSOLE_CHILD_STARTING",
            "OPENCONSOLE_CHILD_RETURNED",
        ]
        if scene == "gles":
            required += ["OPENCONSOLE_GLES_PROVIDER_READY", "OPENCONSOLE_GLES_SCENE_READY", "OPENCONSOLE_GLES_KEY_RECEIVED"]
        elif scene == "io":
            required += ["OPENCONSOLE_SELECTION_CHANGED", "OPENCONSOLE_CHILD_READY",
                         "OPENCONSOLE_CHILD_KEY_RECEIVED", "OPENCONSOLE_IO_MOCK_PROVIDERS_READY",
                         "OPENCONSOLE_IO_CHILD_RESOURCES_LIVE", "OPENCONSOLE_IO_PARENT_RECLAIMING",
                         "OPENCONSOLE_IO_PARENT_CLEAN"]
        elif scene == "network":
            required += ["OPENCONSOLE_SELECTION_CHANGED", "OPENCONSOLE_CHILD_READY",
                         "OPENCONSOLE_CHILD_KEY_RECEIVED", "OPENCONSOLE_NET_PROVIDER_READY",
                         "OPENCONSOLE_NET_GENERATION_CLEAN"]
        elif scene == "dashboard":
            required += ["OPENCONSOLE_DASHBOARD_HEADER_READY",
                         "OPENCONSOLE_DASHBOARD_QUERY_second",
                         "OPENCONSOLE_DASHBOARD_RESULTS_0000000000000001",
                         "OPENCONSOLE_DASHBOARD_LAUNCH_ID_second_probe",
                         "OPENCONSOLE_CHILD_READY", "OPENCONSOLE_CHILD_KEY_RECEIVED"]
        elif scene == "pointer-pagination":
            required += ["OPENCONSOLE_POINTER_READY",
                         "OPENCONSOLE_POINTER_PAGE_CHANGED_0000000000000002",
                         "OPENCONSOLE_DASHBOARD_FILTER_RECONCILED",
                         "OPENCONSOLE_DASHBOARD_QUERY_09",
                         "OPENCONSOLE_DASHBOARD_LAUNCH_ID_library_09",
                         "OPENCONSOLE_POINTER_RESET_AFTER_CHILD",
                         "OPENCONSOLE_CHILD_READY", "OPENCONSOLE_CHILD_KEY_RECEIVED"]
        elif scene == "crypto":
            required += ["OPENCONSOLE_CRYPTO_PROVIDER_READY",
                         "OPENCONSOLE_CRYPTO_CHILD_PROTOCOL_OK",
                         "OPENCONSOLE_CRYPTO_CHILD_SHA256_OK",
                         "OPENCONSOLE_CRYPTO_CHILD_ED448_OK",
                         "OPENCONSOLE_CRYPTO_CHILD_CORRUPTION_REJECTED",
                         "OPENCONSOLE_UPDATE_CHILD_VALID_OK",
                         "OPENCONSOLE_UPDATE_CHILD_EXPIRED_REJECTED",
                         "OPENCONSOLE_UPDATE_CHILD_HOST_KEY_REJECTED",
                         "OPENCONSOLE_UPDATE_CHILD_ROLLBACK_TRUNCATION_CORRUPTION_REJECTED"]
        elif scene == "update-storage":
            required += ["OPENCONSOLE_CRYPTO_PROVIDER_READY",
                         "OPENCONSOLE_UPDATE_STORAGE_READY",
                         "OPENCONSOLE_UPDATE_STORAGE_FIRST_COMMITTED",
                         "OPENCONSOLE_UPDATE_STORAGE_SECOND_COMMITTED",
                         "OPENCONSOLE_UPDATE_STORAGE_LKG_RECOVERED"]
        elif scene == "update-e2e":
            required += ["OPENCONSOLE_TLS_CA_INSTALLED",
                         "OPENCONSOLE_NET_STACK_PACKAGED_LOADED",
                         "OPENCONSOLE_NET_PROVIDER_READY",
                         "OPENCONSOLE_CRYPTO_PROVIDER_READY",
                         "OPENCONSOLE_UPDATE_E2E_DHCP_OK",
                         "OPENCONSOLE_UPDATE_E2E_CATALOG_OK",
                         "OPENCONSOLE_UPDATE_E2E_DOWNLOADS_OK",
                         "OPENCONSOLE_UPDATE_E2E_COMMITTED",
                         "OPENCONSOLE_UPDATE_E2E_REJECTIONS_PRESERVED",
                         "OPENCONSOLE_NET_GENERATION_CLEAN"]
        elif scene == "https":
            required += ["OPENCONSOLE_TLS_CA_INSTALLED",
                         "OPENCONSOLE_NET_STACK_PACKAGED_LOADED",
                         "OPENCONSOLE_NET_PROVIDER_READY",
                         "OPENCONSOLE_HTTPS_VALID_OK",
                         "OPENCONSOLE_HTTPS_EXPIRED_REJECTED",
                         "OPENCONSOLE_HTTPS_WRONG_HOST_REJECTED",
                         "OPENCONSOLE_NET_GENERATION_CLEAN"]
        elif scene == "network-fixture":
            required += ["OPENCONSOLE_NET_PROVIDER_READY", "OPENCONSOLE_NET_CHILD_DHCP_OK",
                         "OPENCONSOLE_NET_CHILD_DNS_ADDRESS_0100007F",
                         "OPENCONSOLE_NET_CHILD_DNS_OK", "OPENCONSOLE_NET_CHILD_UDP_OK",
                         "OPENCONSOLE_NET_CHILD_HTTP_OK", "OPENCONSOLE_NET_CHILD_BOUNDS_OK",
                         "OPENCONSOLE_NET_CHILD_CANCEL_OK", "OPENCONSOLE_NET_CHILD_TIMEOUT_OK",
                         "OPENCONSOLE_NET_CHILD_COMPLETE", "OPENCONSOLE_NET_GENERATION_CLEAN"]
            if os.environ.get("OPENCONSOLE_TEST_PACKAGED_NETWORK") == "1":
                required.append("OPENCONSOLE_NET_STACK_PACKAGED_LOADED")
        elif scene == "physical-network":
            required += ["OPENCONSOLE_TLS_CA_INSTALLED",
                         "OPENCONSOLE_NET_STACK_PACKAGED_LOADED",
                         "OPENCONSOLE_NET_PROVIDER_READY",
                         "OPENCONSOLE_PHYSICAL_NET_DHCP_OK_USEC_",
                         "OPENCONSOLE_PHYSICAL_NET_DNS_OK_USEC_",
                         "OPENCONSOLE_PHYSICAL_NET_UDP_OK_USEC_",
                         "OPENCONSOLE_PHYSICAL_NET_HTTPS_OK_USEC_",
                         "OPENCONSOLE_PHYSICAL_NET_RUN_COMPLETE",
                         "OPENCONSOLE_NET_GENERATION_CLEAN"]
        elif scene == "audio":
            required += ["OPENCONSOLE_AUDIO_PROVIDER_READY",
                         "OPENCONSOLE_AUDIO_CHILD_READY",
                         "OPENCONSOLE_AUDIO_CHILD_REFERENCE_COMPLETE",
                         "OPENCONSOLE_AUDIO_PARENT_RECLAIMING",
                         "OPENCONSOLE_AUDIO_PARENT_CLEAN"]
        elif scene == "integrated":
            required += ["OPENCONSOLE_NET_PROVIDER_READY", "OPENCONSOLE_AUDIO_PROVIDER_READY",
                         "OPENCONSOLE_INTEGRATED_CHILD_READY",
                         "OPENCONSOLE_INTEGRATED_EFFECT_TRIGGERED",
                         "OPENCONSOLE_INTEGRATED_NETWORK_OK", "OPENCONSOLE_INTEGRATED_FRAME",
                         "OPENCONSOLE_INTEGRATED_CHILD_RETURNING",
                         "OPENCONSOLE_INTEGRATED_CHILD_ERROR_RETURNING",
                         "OPENCONSOLE_CHILD_ERROR_RETURNED",
                         "OPENCONSOLE_AUDIO_PARENT_RECLAIMING",
                         "OPENCONSOLE_AUDIO_PARENT_CLEAN",
                         "OPENCONSOLE_NET_GENERATION_CLEAN"]
        elif scene == "offline":
            required += ["OPENCONSOLE_SELECTION_CHANGED", "OPENCONSOLE_CHILD_READY",
                         "OPENCONSOLE_CHILD_KEY_RECEIVED", "OPENCONSOLE_NET_OFFLINE_NO_BACKEND"]
        else:
            required += ["OPENCONSOLE_SELECTION_CHANGED", "OPENCONSOLE_CHILD_READY", "OPENCONSOLE_CHILD_KEY_RECEIVED"]
        if gpu_mode == "ati-probe" and scene not in (
                "pointer-pagination", "game-details", "integrated", "slice"):
            required.append("OPENCONSOLE_AMD_PCI_1002_5159")
        missing = [marker for marker in required if not log_contains(log, marker)]
        if missing:
            raise RuntimeError(f"missing lifecycle markers: {', '.join(missing)}")
        if scene == "network-fixture":
            repeated = [marker for marker in ("OPENCONSOLE_NET_CHILD_COMPLETE",
                                               "OPENCONSOLE_NET_GENERATION_CLEAN")
                        if log_count(log, marker) != 2]
            if repeated:
                raise RuntimeError(f"network lifecycle markers did not repeat twice: {repeated}")
            fixture.verify()
            operation_markers = {
                "dhcp": "OPENCONSOLE_NET_CHILD_DHCP_USEC_",
                "dns": "OPENCONSOLE_NET_CHILD_DNS_USEC_",
                "udp": "OPENCONSOLE_NET_CHILD_UDP_USEC_",
                "http": "OPENCONSOLE_NET_CHILD_HTTP_USEC_",
                "bounds": "OPENCONSOLE_NET_CHILD_BOUNDS_USEC_",
                "timeout": "OPENCONSOLE_NET_CHILD_TIMEOUT_USEC_",
            }
            operations = {name: log_hex_values(log, marker)
                          for name, marker in operation_markers.items()}
            if any(len(values) != 2 for values in operations.values()):
                raise RuntimeError(f"missing per-operation latency evidence: {operations}")
            peak = log_hex_values(log, "OPENCONSOLE_NET_ALLOC_PEAK_BYTES_")
            live_bytes = log_hex_values(log, "OPENCONSOLE_NET_ALLOC_LIVE_BYTES_")
            live_count = log_hex_values(log, "OPENCONSOLE_NET_ALLOC_LIVE_COUNT_")
            if len(peak) != 2 or live_bytes != [0, 0] or live_count != [0, 0]:
                raise RuntimeError("network allocation teardown evidence is incomplete")
            metrics = {
                "schema": 1,
                "scope": "OVMF virtio-net; OpenConsole provider allocations only",
                "network_stack": ("packaged-edk2" if os.environ.get(
                    "OPENCONSOLE_TEST_PACKAGED_NETWORK") == "1" else "firmware-or-packaged"),
                "operation_latency_usec": operations,
                "child_wall_seconds": child_elapsed,
                "provider_peak_bytes": peak,
                "provider_live_bytes_after_cleanup": live_bytes,
                "provider_live_allocations_after_cleanup": live_count,
            }
            metrics_path = os.path.join(os.path.dirname(log),
                                        "network-fixture-metrics.json")
            with open(metrics_path, "w", encoding="utf-8") as stream:
                json.dump(metrics, stream, indent=2, sort_keys=True)
                stream.write("\n")
        if scene == "physical-network":
            fixture.verify_physical()
            https_fixture.verify()
            evidence_path = os.path.join(esp, "OPENCONSOLE-NETWORK-EVIDENCE.LOG")
            with open(evidence_path, "rb") as stream:
                physical_evidence = stream.read()
            for marker in (b"OPENCONSOLE_PHYSICAL_NET_DHCP_OK_USEC_",
                           b"OPENCONSOLE_PHYSICAL_NET_DNS_OK_USEC_",
                           b"OPENCONSOLE_PHYSICAL_NET_UDP_OK_USEC_",
                           b"OPENCONSOLE_PHYSICAL_NET_HTTPS_OK_USEC_",
                           b"OPENCONSOLE_PHYSICAL_NET_RUN_COMPLETE"):
                if marker not in physical_evidence:
                    raise RuntimeError(
                        f"persistent physical-network evidence missing {marker!r}")
        if scene == "integrated":
            repeated = [marker for marker in (
                "OPENCONSOLE_INTEGRATED_CHILD_READY",
                "OPENCONSOLE_INTEGRATED_NETWORK_OK",
                "OPENCONSOLE_AUDIO_PARENT_CLEAN",
                "OPENCONSOLE_NET_GENERATION_CLEAN")
                if log_count(log, marker) != 2]
            if repeated:
                raise RuntimeError(f"integrated lifecycle markers did not repeat twice: {repeated}")
            once = [marker for marker in ("OPENCONSOLE_INTEGRATED_CHILD_RETURNING",
                                           "OPENCONSOLE_INTEGRATED_CHILD_ERROR_RETURNING")
                    if log_count(log, marker) != 1]
            if once:
                raise RuntimeError(f"normal/error return evidence is incomplete: {once}")
            if (log_count(log, "OPENCONSOLE_SLICE_MOUSE_SOUND") < 1 or
                    log_count(log, "OPENCONSOLE_INTEGRATED_EFFECT_TRIGGERED") != 1):
                raise RuntimeError("mouse/keyboard audio triggers were not both observed")
            fixture.verify_integrated()
            underruns = log_hex_values(log, "OPENCONSOLE_INTEGRATED_AUDIO_UNDERRUNS_")
            latency = log_hex_values(log, "OPENCONSOLE_INTEGRATED_AUDIO_LATENCY_FRAMES_")
            frames = log_hex_values(log, "OPENCONSOLE_INTEGRATED_FRAME_COUNT_")
            dhcp_usec = log_hex_values(log, "OPENCONSOLE_INTEGRATED_DHCP_USEC_")
            udp_usec = log_hex_values(log, "OPENCONSOLE_INTEGRATED_UDP_USEC_")
            if len(underruns) != 2 or any(value > 1 for value in underruns):
                raise RuntimeError(f"integrated audio underruns exceeded one-buffer VM budget: {underruns}")
            # AudioIo rotates three 4096-frame buffers; the VM budget is one
            # complete triple-buffer pipeline (256 ms at 48 kHz).
            if len(latency) != 2 or any(value > 12288 for value in latency):
                raise RuntimeError(f"integrated audio latency exceeded 12288 frames: {latency}")
            if len(frames) != 2 or any(value < 16 for value in frames):
                raise RuntimeError(f"integrated GLES progress was insufficient: {frames}")
            if len(dhcp_usec) != 2 or any(value > 15000000 for value in dhcp_usec):
                raise RuntimeError(f"integrated DHCP latency exceeded deadline: {dhcp_usec}")
            if len(udp_usec) != 2 or any(value > 5000000 for value in udp_usec):
                raise RuntimeError(f"integrated UDP latency exceeded deadline: {udp_usec}")
            metrics = {
                "schema": 1,
                "scope": "OVMF simultaneous GLES/audio/input/virtio-net child",
                "launches": 2,
                "gles_frames_before_return": frames,
                "gles_visual_progress": True,
                "audio_underruns": underruns,
                "audio_latency_frames": latency,
                "audio_latency_budget_frames": 12288,
                "dhcp_latency_usec": dhcp_usec,
                "udp_latency_usec": udp_usec,
                "input_effect_triggers": 2,
                "keyboard_mapping_verified": True,
                "simple_pointer_verified": True,
                "normal_and_error_cleanup": True,
            }
            with open(os.path.join(os.path.dirname(log),
                                   "ovmf-integrated-io-metrics.json"),
                      "w", encoding="utf-8") as stream:
                json.dump(metrics, stream, indent=2, sort_keys=True)
                stream.write("\n")
        if scene == "https":
            https_fixture.verify()
        if scene in ("update-storage", "update-e2e") and file_sha256(active_boot_path) != active_boot_before:
            raise RuntimeError("active BOOTX64.EFI bytes changed during staged update")
        if scene == "update-e2e":
            https_fixture.verify_update()
        if scene in ("audio", "integrated") and (not os.path.isfile(wav_capture) or
                                  os.path.getsize(wav_capture) <= 44):
            raise RuntimeError("QEMU WAV capture is missing or empty")
        if scene in ("audio", "integrated"):
            metrics_name = ("ovmf-integrated-audio-metrics.json" if scene == "integrated"
                            else "ovmf-audio-metrics.json")
            metrics_path = os.path.join(os.path.dirname(log), metrics_name)
            analyzer = os.path.join(os.path.dirname(__file__), "analyze-ovmf-audio.py")
            subprocess.run([sys.executable, analyzer, wav_capture, metrics_path, scene], check=True)
        if not os.path.isfile(screenshot) or os.path.getsize(screenshot) < 1024:
            raise RuntimeError("GOP screenshot was not captured")
        ppm_to_png(screenshot, png_screenshot)
        if scene in ("dashboard", "pointer-pagination"):
            if not os.path.isfile(filtered_screenshot) or os.path.getsize(
                    filtered_screenshot) < 1024:
                raise RuntimeError("filtered dashboard screenshot was not captured")
            ppm_to_png(filtered_screenshot, filtered_png)
        if scene == "pointer-pagination":
            if not os.path.isfile(page_two_screenshot) or os.path.getsize(
                    page_two_screenshot) < 1024:
                raise RuntimeError("page-two pointer screenshot was not captured")
            ppm_to_png(page_two_screenshot, page_two_png)
        if scene == "gles":
            print(f"PASS: software GLES triangle presented, grid MAE={image_mae:.2f}, child returned")
        elif scene == "network-fixture":
            print("PASS: virtio-net DHCP/DNS/UDP/HTTP/bounds/timeout and two clean child launches")
        elif scene == "physical-network":
            print("PASS: configurable physical-network child persisted DHCP/DNS/UDP/HTTPS evidence")
        elif scene == "audio":
            print(f"PASS: AudioDxe/HDA playback captured and child DMA lifecycle cleaned: {wav_capture}")
        elif scene == "integrated":
            print("PASS: two integrated graphics/input/network/audio child launches and clean parent teardown")
        elif scene == "crypto":
            print("PASS: parent-loaded CryptoPkg protocol passed SHA-256, Ed448, and corruption rejection in OVMF")
        elif scene == "update-storage":
            print("PASS: UEFI FAT staging, flush/readback, atomic selection, recovery, and active-EFI preservation")
        elif scene == "update-e2e":
            print("PASS: catalog/HTTPS/signature/staging update flow and rejection preservation in OVMF")
        elif scene == "https":
            print("PASS: HttpDxe accepted valid TLS identity and rejected expired/wrong-host certificates")
        elif scene == "dashboard":
            print("PASS: dashboard header, title search, filtered launch, and parent recovery")
            print(f"FILTERED SCREENSHOT: {filtered_png}")
        elif scene == "pointer-pagination":
            print("PASS: UEFI mouse pagination, live filtering, exact click launch, and parent recovery")
            print(f"PAGE TWO SCREENSHOT: {page_two_png}")
            print(f"FILTERED SCREENSHOT: {filtered_png}")
        else:
            print("PASS: parent menu rendered, child received keyboard input, parent resumed")
        print(f"SCREENSHOT: {png_screenshot}")
        return 0
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        if os.path.exists(log):
            with open(log, "r", encoding="utf-8", errors="replace") as stream:
                print(stream.read()[-8000:], file=sys.stderr)
        return 1
    finally:
        if qmp is not None:
            qmp.close()
        if vnc_pointer is not None:
            vnc_pointer.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if fixture is not None:
            fixture.close()
        if https_fixture is not None:
            https_fixture.close()
        shutil.rmtree(qmp_tempdir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
