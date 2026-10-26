#!/usr/bin/env python3
import datetime
import ipaddress
import os
import sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)

def write_key(path, value):
    with open(path, "wb") as stream:
        stream.write(value.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()))

def write_cert(path, value, encoding=serialization.Encoding.PEM):
    with open(path, "wb") as stream:
        stream.write(value.public_bytes(encoding))

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate-tls-fixtures.py OUTPUT_DIRECTORY")
    output = sys.argv[1]
    os.makedirs(output, exist_ok=True)
    now = datetime.datetime.now(datetime.timezone.utc)
    ca_key = key()
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "OpenConsole Test CA")])
    ca = (x509.CertificateBuilder().subject_name(ca_name).issuer_name(ca_name)
          .public_key(ca_key.public_key()).serial_number(1)
          .not_valid_before(now - datetime.timedelta(days=30))
          .not_valid_after(now + datetime.timedelta(days=30))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True,
                                      True, False, False), critical=True)
          .sign(ca_key, hashes.SHA256()))
    write_cert(os.path.join(output, "ca.der"), ca, serialization.Encoding.DER)
    write_cert(os.path.join(output, "ca.pem"), ca)
    for name, address, before, after, serial in (
        ("valid", "10.0.2.2", now - datetime.timedelta(days=1),
         now + datetime.timedelta(days=1), 2),
        ("wrong-host", "10.0.2.3", now - datetime.timedelta(days=1),
         now + datetime.timedelta(days=1), 3),
        ("expired", "10.0.2.2", now - datetime.timedelta(days=3),
         now - datetime.timedelta(days=2), 4)):
        leaf_key = key()
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, address)])
        leaf = (x509.CertificateBuilder().subject_name(subject).issuer_name(ca.subject)
                .public_key(leaf_key.public_key()).serial_number(serial)
                .not_valid_before(before).not_valid_after(after)
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.SubjectAlternativeName([
                    x509.IPAddress(ipaddress.ip_address(address))]), critical=False)
                .add_extension(x509.ExtendedKeyUsage([
                    x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                .sign(ca_key, hashes.SHA256()))
        write_key(os.path.join(output, name + ".key"), leaf_key)
        write_cert(os.path.join(output, name + ".pem"), leaf)

if __name__ == "__main__":
    main()
