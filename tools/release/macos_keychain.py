#!/usr/bin/env python3
"""Import a Developer ID identity into an isolated signing-job keychain."""
import base64
import hashlib
import os
from pathlib import Path
import re
import secrets
import subprocess
import urllib.request

INTERMEDIATE_URL = 'https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer'
INTERMEDIATE_SHA256 = 'f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a'


def checked(arguments, label):
    # CalledProcessError includes arguments, which can contain import passwords.
    result = subprocess.run(arguments, capture_output=True, text=True)
    if result.returncode:
        raise ValueError(label + ' failed; credential values are withheld')
    return result.stdout


def prepare(temp, p12, p12_password, notary_key, identity, intermediate):
    if not identity.startswith('Developer ID Application:'):
        raise ValueError('Direct downloads require a Developer ID Application identity')
    if hashlib.sha256(intermediate).hexdigest() != INTERMEDIATE_SHA256:
        raise ValueError('Apple Developer ID G2 intermediate checksum mismatch')
    os.umask(0o077)
    temp = Path(temp)
    keychain = temp / 'glob2-signing.keychain-db'
    certificate = temp / 'glob2-developer-id.p12'
    ca = temp / 'glob2-developer-id-g2.cer'
    key = temp / 'glob2-notary.p8'
    if any(path.exists() for path in (keychain, certificate, ca, key)):
        raise ValueError('Signing material already exists; refusing to overwrite it')
    certificate.write_bytes(p12)
    ca.write_bytes(intermediate)
    key.write_bytes(notary_key)
    password = secrets.token_urlsafe(32)
    checked(['security', 'create-keychain', '-p', password, str(keychain)], 'Keychain creation')
    checked(['security', 'set-keychain-settings', '-lut', '21600', str(keychain)], 'Keychain timeout')
    checked(['security', 'unlock-keychain', '-p', password, str(keychain)], 'Keychain unlock')
    existing = re.findall(r'"([^"\n]+)"', checked(['security', 'list-keychains', '-d', 'user'], 'Keychain search'))
    checked(['security', 'list-keychains', '-d', 'user', '-s', str(keychain), *existing], 'Keychain search-list update')
    checked(['security', 'import', str(ca), '-k', str(keychain)], 'Developer ID intermediate import')
    checked(['security', 'import', str(certificate), '-k', str(keychain), '-P', p12_password,
             '-T', '/usr/bin/codesign'], 'Developer ID identity import')
    checked(['security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:', '-s',
             '-k', password, str(keychain)], 'Signing key access')
    identities = checked(['security', 'find-identity', '-v', '-p', 'codesigning', str(keychain)], 'Signing identity verification')
    if identity not in re.findall(r'"([^"\n]+)"', identities):
        raise ValueError('Configured Developer ID identity is not valid in the temporary keychain')
    certificate.unlink()
    ca.unlink()
    return keychain, key


def main():
    required = ('RUNNER_TEMP', 'P12', 'P12_PASSWORD', 'NOTARY_KEY', 'IDENTITY')
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise ValueError('Missing signing configuration: ' + ', '.join(missing))
    with urllib.request.urlopen(INTERMEDIATE_URL, timeout=30) as response:
        intermediate = response.read(65537)
    prepare(os.environ['RUNNER_TEMP'], base64.b64decode(os.environ['P12'], validate=True),
            os.environ['P12_PASSWORD'], base64.b64decode(os.environ['NOTARY_KEY'], validate=True),
            os.environ['IDENTITY'], intermediate)
    print('Developer ID chain and private-key identity verified in temporary keychain')


if __name__ == '__main__':
    main()
