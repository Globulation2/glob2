#!/usr/bin/env python3
"""Exercise the mobile TLS verification boundary with an isolated certificate."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
scratch = ROOT/'build/test-profiles'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='certificate-', dir=scratch) as directory:
    directory = Path(directory)
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
        '-keyout', str(directory/'key.pem'), '-out', str(directory/'certificate.pem'),
        '-subj', '/CN=mobile.test', '-addext', 'subjectAltName=DNS:mobile.test'],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    subprocess.run([str(args.binary.resolve()), str(directory/'certificate.pem'), 'mobile.test'], check=True)
