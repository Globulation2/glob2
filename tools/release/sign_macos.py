#!/usr/bin/env python3
"""Validate, Developer ID sign, notarize and staple a direct-download app.

Run only in the protected mirror signing job. Credentials remain in temporary
runner storage; final images are produced only after accepted notarization.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

MACHO = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}


def is_macho(path):
    with path.open("rb") as stream:
        return stream.read(4) in MACHO


def binaries(app):
    return sorted((p for p in app.rglob('*') if p.is_file() and not p.is_symlink()
                   and is_macho(p)), key=lambda p: len(p.parts), reverse=True)


def validate(app, architecture):
    found = binaries(app)
    if not found:
        raise ValueError('Application has no Mach-O binaries')
    for binary in found:
        archs = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
        if archs != [architecture]:
            raise ValueError(f'Unexpected architecture in {binary.name}')
        loads = subprocess.check_output(['otool', '-l', str(binary)], text=True)
        # Read only platform-version commands, not dylib compatibility versions.
        sections = re.split(r'Load command \d+', loads)
        versions = [re.search(r'\b(?:minos|version)\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)', section)
                    for section in sections if 'LC_BUILD_VERSION' in section or 'LC_VERSION_MIN_MACOSX' in section]
        if not versions or any(not match or tuple(map(int, match[1].split('.')[:2])) > (15, 0) for match in versions):
            raise ValueError(f'{binary.name} does not support macOS 15.0')
    return found


def sign(app, output, architecture, identity, keychain, key, key_id, issuer):
    if not identity.startswith('Developer ID Application:'):
        raise ValueError('Direct downloads require a Developer ID Application identity')
    for path in [*validate(app, architecture), app]:
        subprocess.run(['codesign', '--force', '--sign', identity, '--keychain', str(keychain),
                        '--timestamp', '--options', 'runtime', str(path)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    with tempfile.TemporaryDirectory() as temp:
        archive = Path(temp) / 'notarize.zip'
        subprocess.run(['ditto', '-c', '-k', '--keepParent', str(app), str(archive)], check=True)
        auth = ['--key', str(key), '--key-id', key_id, '--issuer', issuer]
        result = subprocess.check_output(['xcrun', 'notarytool', 'submit', str(archive), '--wait',
                                          '--output-format', 'json', *auth], text=True)
        if json.loads(result).get('status') != 'Accepted':
            raise ValueError('App notarization was not accepted')
        subprocess.run(['xcrun', 'stapler', 'staple', str(app)], check=True)
        subprocess.run(['spctl', '--assess', '--type', 'execute', str(app)], check=True)
        stage = Path(temp) / 'stage'; stage.mkdir()
        subprocess.run(['ditto', str(app), str(stage/app.name)], check=True)
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['hdiutil', 'create', '-srcfolder', str(stage), '-format', 'UDZO', str(output)], check=True)
        subprocess.run(['codesign', '--sign', identity, '--keychain', str(keychain), '--timestamp', str(output)], check=True)
        result = subprocess.check_output(['xcrun', 'notarytool', 'submit', str(output), '--wait',
                                          '--output-format', 'json', *auth], text=True)
        if json.loads(result).get('status') != 'Accepted':
            raise ValueError('DMG notarization was not accepted')
        subprocess.run(['xcrun', 'stapler', 'staple', str(output)], check=True)
        subprocess.run(['xcrun', 'stapler', 'validate', str(output)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('app', 'output', 'keychain', 'key'):
        parser.add_argument('--'+arg, type=Path, required=True)
    for arg in ('architecture', 'identity', 'key-id', 'issuer'):
        parser.add_argument('--'+arg, required=True)
    args=vars(parser.parse_args()); sign(**args)
