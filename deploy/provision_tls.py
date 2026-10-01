#!/usr/bin/env python3
"""Provision an isolated deployment CA and lobby/router identities. Never overwrites keys."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

def provision(destination):
    destination = Path(destination).resolve()
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    if any(destination.iterdir()):
        raise ValueError('Refusing to replace existing TLS material; use a new empty directory for rotation')
    os.chmod(destination, 0o700)
    with tempfile.TemporaryDirectory(dir=destination) as temporary:
        work = Path(temporary)
        def run(*args):
            subprocess.run(['openssl', *map(str, args)], check=True, capture_output=True)
        run('req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
            '-nodes', '-keyout', work/'ca.key', '-out', work/'ca.pem', '-days', '3650',
            '-subj', '/CN=Glob2 deployment CA', '-addext', 'basicConstraints=critical,CA:TRUE',
            '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
        for service in ('lobby', 'router'):
            run('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1',
                '-nodes', '-keyout', work/f'{service}.key', '-out', work/f'{service}.csr',
                '-subj', f'/CN={service}')
            extension = work/f'{service}.ext'
            extension.write_text(f'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n'
                f'extendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:{service},DNS:localhost,IP:127.0.0.1\n')
            run('x509', '-req', '-in', work/f'{service}.csr', '-CA', work/'ca.pem', '-CAkey', work/'ca.key',
                '-CAcreateserial', '-out', work/f'{service}.pem', '-days', '90', '-sha256', '-extfile', extension)
        for name in ('ca.pem', 'ca.key', 'lobby.pem', 'lobby.key', 'router.pem', 'router.key'):
            source = work/name
            # Compose file-backed secrets retain host file ownership. Directory
            # traversal is owner-only; mounted keys must be readable by UID 10001.
            source.chmod(0o600 if name == 'ca.key' else 0o444)
            source.rename(destination/name)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    provision(parser.parse_args().directory)
