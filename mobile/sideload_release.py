#!/usr/bin/env python3
"""Sign an already verified unsigned APK with the permanent sideload identity."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

from android_release import verify_apk
from developer_apk import build_tools, java_environment, verify_alignment
from dev_paths import android_sdk

ROOT=Path(__file__).resolve().parents[1]


def sign(original, output, arch, keystore, alias, expected_certificate, sdk):
    if alias in ('androiddebugkey', 'glob2-upload'):
        raise ValueError('Sideload signing must use its own permanent key')
    fingerprint = expected_certificate.lower().replace(':', '')
    if not re.fullmatch('[0-9a-f]{64}', fingerprint):
        raise ValueError('A pinned sideload signing certificate SHA-256 is required')
    for name in ('GLOB2_SIDELOAD_STORE_PASSWORD', 'GLOB2_SIDELOAD_KEY_PASSWORD'):
        if not os.environ.get(name):
            raise ValueError(f'Missing {name}')
    # Signing job has the APK, not build dependency manifests. Native/assets,
    # version and alignment checks still run before signing.
    verify_apk(original, arch, sdk, require_dependency_manifest=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    tool = str(build_tools(ROOT,sdk)/'apksigner')
    env = java_environment(ROOT)
    subprocess.run([tool, 'sign', '--ks', str(keystore), '--ks-key-alias', alias,
                    '--ks-pass', 'env:GLOB2_SIDELOAD_STORE_PASSWORD',
                    '--key-pass', 'env:GLOB2_SIDELOAD_KEY_PASSWORD',
                    '--v4-signing-enabled', 'false', '--out', str(output), str(original)], env=env, check=True)
    report = subprocess.check_output([tool, 'verify', '--verbose', '--print-certs', str(output)], env=env, text=True)
    certificates = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)', report)
    if certificates != [fingerprint]:
        output.unlink(missing_ok=True)
        raise ValueError('Signed APK certificate does not match the permanent sideload identity')
    verify_alignment(ROOT, sdk, output)
    return hashlib.sha256(output.read_bytes()).hexdigest()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('original','output','keystore'): parser.add_argument('--'+name,type=Path,required=True)
    for name in ('arch','alias','expected-certificate'): parser.add_argument('--'+name,required=True)
    args=vars(parser.parse_args()); args['sdk']=android_sdk(ROOT,None)
    print(sign(**args))
