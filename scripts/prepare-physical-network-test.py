#!/usr/bin/env python3
import argparse
import datetime
import ipaddress
import json
import secrets
import shutil
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare an endpoint-specific OpenConsole physical network test")
    parser.add_argument("endpoint_ip", type=ipaddress.ip_address)
    parser.add_argument("--esp", type=Path, default=Path("build/esp"))
    parser.add_argument("--fixture-dir", type=Path,
                        default=Path("build/physical-network-fixture"))
    parser.add_argument("--dns-name", default="example.com")
    parser.add_argument("--expected-dns-ip", type=ipaddress.ip_address)
    parser.add_argument("--token", default=None)
    args = parser.parse_args()
    if args.endpoint_ip.version != 4:
        parser.error("endpoint_ip must be IPv4")
    if args.expected_dns_ip and args.expected_dns_ip.version != 4:
        parser.error("expected DNS address must be IPv4")
    boot = args.esp / "EFI/BOOT/BOOTX64.EFI"
    if not boot.is_file():
        raise SystemExit(f"FAIL: physical ESP is absent: {boot}; run make physical-test-esp")

    token = args.token or secrets.token_hex(12)
    if not token.isascii() or not token.isalnum() or len(token) > 48:
        parser.error("token must be 1-48 ASCII alphanumeric characters")
    request = f"openconsole-physical-udp:{token}"
    udp_response = f"openconsole-physical-udp-ok:{token}"
    https_response = f"openconsole-physical-https-ok:{token}"
    args.fixture_dir.mkdir(parents=True, exist_ok=True)

    now = datetime.datetime.now(datetime.timezone.utc)
    expires = now + datetime.timedelta(days=2)
    ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    ca_name = x509.Name([x509.NameAttribute(
        NameOID.COMMON_NAME, "OpenConsole Physical Test CA")])
    ca = (x509.CertificateBuilder().subject_name(ca_name).issuer_name(ca_name)
          .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
          .not_valid_before(now - datetime.timedelta(days=1))
          .not_valid_after(now + datetime.timedelta(days=7))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True,
                                      True, False, False), critical=True)
          .sign(ca_key, hashes.SHA256()))
    server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,
                                             str(args.endpoint_ip))])
    server = (x509.CertificateBuilder().subject_name(subject).issuer_name(ca.subject)
              .public_key(server_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now - datetime.timedelta(hours=1))
              .not_valid_after(now + datetime.timedelta(days=2))
              .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
              .add_extension(x509.SubjectAlternativeName([
                  x509.IPAddress(args.endpoint_ip)]), critical=False)
              .add_extension(x509.ExtendedKeyUsage([
                  ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
              .sign(ca_key, hashes.SHA256()))

    ca_der = ca.public_bytes(serialization.Encoding.DER)
    (args.fixture_dir / "ca.der").write_bytes(ca_der)
    (args.fixture_dir / "server.pem").write_bytes(
        server.public_bytes(serialization.Encoding.PEM))
    (args.fixture_dir / "server.key").write_bytes(server_key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption()))
    fixture = {
        "schema": 1, "endpoint_ipv4": str(args.endpoint_ip), "token": token,
        "created_utc": now.isoformat(), "expires_utc": expires.isoformat(),
        "udp_port": 9000, "https_port": 9443, "udp_request": request,
        "udp_response": udp_response, "https_response": https_response,
    }
    (args.fixture_dir / "fixture.json").write_text(
        json.dumps(fixture, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    trust = args.esp / "EFI/OpenConsole/Trust"
    trust.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.fixture_dir / "ca.der", trust / "ca.der")
    expected_dns = str(args.expected_dns_ip) if args.expected_dns_ip else ""
    config = (
        f"dns_name={args.dns_name}\n"
        f"expected_dns_ipv4={expected_dns}\n"
        f"endpoint_ipv4={args.endpoint_ip}\n"
        "udp_port=9000\n"
        f"udp_request={request}\n"
        f"udp_response={udp_response}\n"
        f"https_url=https://{args.endpoint_ip}:9443/probe?token={token}\n"
        f"https_response={https_response}\n"
    )
    (args.esp / "OPENCONSOLE-NETWORK.CFG").write_text(config, encoding="ascii")
    print(f"Prepared USB config in {args.esp}")
    print(f"Copy {args.fixture_dir} to the second host and run:")
    print(f"  ./scripts/run-physical-network-fixture.py {args.fixture_dir}")
    print("Repackage after preparation with: ./scripts/package-physical-test-esp.sh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
