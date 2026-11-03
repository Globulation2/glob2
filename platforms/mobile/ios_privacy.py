#!/usr/bin/env python3
"""Reject unused camera and Bluetooth APIs in the final iOS app before upload."""
import argparse
from pathlib import Path
import re
import subprocess


MACHO = {bytes.fromhex(value) for value in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
UNUSED_CLASSES = re.compile(r'\b(?:AVCapture\w*|CBCentralManager|CBPeripheral\w*|CBManager|CBUUID)\b')


def check_app(app, run=subprocess.check_output):
    app = Path(app)
    if not app.is_dir():
        raise ValueError('Expected an extracted iOS .app directory')
    checked = []
    for path in sorted(app.rglob('*')):
        if not path.is_file():
            continue
        with path.open('rb') as source:
            magic = source.read(4)
        if magic not in MACHO:
            continue
        # Dynamic Objective-C imports survive stripping. Inspect embedded
        # frameworks as well as the main executable, not just source settings.
        symbols = run(['nm', '-u', str(path)], text=True)
        libraries = run(['otool', '-L', str(path)], text=True)
        classes = sorted(set(UNUSED_CLASSES.findall(symbols.replace('_OBJC_CLASS_$_', ' '))))
        if classes or '/CoreBluetooth.framework/' in libraries:
            detail = ', '.join(classes) if classes else 'CoreBluetooth framework'
            raise ValueError(f'{path.relative_to(app)} includes unused privacy APIs: {detail}. '
                             'Rebuild dependencies with camera/HIDAPI disabled and remove unused framework links.')
        checked.append(str(path.relative_to(app)))
    if not checked:
        raise ValueError('No Mach-O binaries found in the iOS app')
    return checked


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    args = parser.parse_args()
    for binary in check_app(args.app):
        print('Privacy API check passed:', binary)
