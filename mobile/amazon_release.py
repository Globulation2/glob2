#!/usr/bin/env python3
"""Verify, sign, and submit the Fire-tablet APK. No credentials are needed to build it."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scons'))
from build_layout import PACKAGE_VERSION
from asset_bundle import verify_apk_assets

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.globulation2.glob2'
ABIS = ('arm64-v8a', 'armeabi-v7a')
API = 'https://developer.amazon.com/api/appstore/v1/applications'
TOKEN = 'https://api.amazon.com/auth/o2/token'


def version_code(version=PACKAGE_VERSION):
    parts = version.split('.')
    if len(parts) != 4 or any(not part.isdecimal() for part in parts):
        raise ValueError('Amazon version must have four numeric components')
    major, minor, patch, revision = map(int, parts)
    if major > 20 or minor > 99 or patch > 99 or revision > 9999:
        raise ValueError('Amazon version component exceeds its reserved range')
    result = major * 100000000 + minor * 1000000 + patch * 10000 + revision
    if not 1 <= result <= 2100000000:
        raise ValueError('Amazon versionCode is outside the Android range')
    return result


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def verify_apk(apk, sdk, expected_version=PACKAGE_VERSION):
    from developer_apk import build_tools, verify_alignment
    if not apk.is_file():
        raise ValueError('Amazon APK is missing')
    verify_alignment(ROOT, sdk, apk)
    verify_apk_assets(apk)
    with zipfile.ZipFile(apk) as archive:
        libraries = {abi: {Path(name).name for name in archive.namelist()
                             if name.startswith(f'lib/{abi}/') and name.endswith('.so')}
                     for abi in ABIS}
        if not libraries[ABIS[0]] or libraries[ABIS[0]] != libraries[ABIS[1]]:
            raise ValueError('Amazon APK has missing or unequal ARM native libraries')
        if any(name.startswith('lib/') and name.split('/')[1] not in ABIS
               for name in archive.namelist()):
            raise ValueError('Amazon APK includes an unexpected native ABI')
    badging = subprocess.check_output([str(build_tools(ROOT, sdk) / 'aapt'), 'dump', 'badging', str(apk)], text=True)
    match = re.search(r"^package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'", badging, re.MULTILINE)
    if not match or (match.group(1), int(match.group(2)), match.group(3)) != (PACKAGE, version_code(expected_version), expected_version):
        raise ValueError('Amazon APK package or version differs from the release')
    return libraries


def prepare(apk, sdk, source_commit, tag, output):
    if not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise ValueError('Source commit must be a full Git SHA')
    if tag != 'v' + PACKAGE_VERSION:
        raise ValueError('Release tag differs from PACKAGE_VERSION')
    verify_apk(apk, sdk)
    metadata = json.loads(apk.with_suffix('.json').read_text())
    if (metadata.get('sha256') != sha256(apk) or metadata.get('version_code') != version_code()
            or metadata.get('version_name') != PACKAGE_VERSION or metadata.get('package') != PACKAGE
            or tuple(metadata.get('architectures', [])) != ABIS):
        raise ValueError('Unsigned APK differs from its build provenance')
    result = dict(metadata, source_commit=source_commit, tag=tag)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    return result


def verify_artifact(apk, manifest, sdk):
    metadata = json.loads(manifest.read_text())
    version = metadata.get('version_name', '')
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
        raise ValueError('Amazon artifact version is invalid')
    verify_apk(apk, sdk, version)
    if (metadata.get('sha256') != sha256(apk) or metadata.get('package') != PACKAGE
            or metadata.get('version_code') != version_code(version)
            or tuple(metadata.get('architectures', [])) != ABIS
            or metadata.get('tag') != 'v' + version
            or not re.fullmatch(r'[0-9a-f]{40}', metadata.get('source_commit', ''))):
        raise ValueError('Amazon artifact provenance mismatch')
    return metadata


def sign(apk, manifest, sdk, keystore, alias, output, expected_cert):
    from developer_apk import build_tools, java_environment
    metadata = verify_artifact(apk, manifest, sdk)
    if not expected_cert or not re.fullmatch(r'[0-9a-fA-F]{64}', expected_cert):
        raise ValueError('Expected signing certificate SHA-256 is required')
    if not os.environ.get('GLOB2_AMAZON_STORE_PASSWORD') or not os.environ.get('GLOB2_AMAZON_KEY_PASSWORD'):
        raise ValueError('Amazon signing passwords are missing')
    if not keystore.is_file():
        raise ValueError('Amazon upload keystore is missing')
    signer = str(build_tools(ROOT, sdk) / 'apksigner')
    subprocess.run([signer, 'sign', '--ks', str(keystore), '--ks-key-alias', alias,
                    '--ks-pass', 'env:GLOB2_AMAZON_STORE_PASSWORD',
                    '--key-pass', 'env:GLOB2_AMAZON_KEY_PASSWORD',
                    '--v4-signing-enabled', 'false', '--out', str(output), str(apk)],
                   env=java_environment(ROOT), check=True)
    verify_apk(output, sdk, metadata['version_name'])
    result = subprocess.check_output([signer, 'verify', '--verbose', '--print-certs', str(output)],
                                     env=java_environment(ROOT), text=True)
    cert = re.search(r'Signer #1 certificate SHA-256 digest: ([0-9a-fA-F]{64})', result)
    if not cert or cert.group(1).lower() != expected_cert.lower():
        output.unlink(missing_ok=True)
        raise ValueError('Amazon upload signing certificate differs from the pinned certificate')
    signed = dict(metadata, unsigned_sha256=metadata['sha256'], sha256=sha256(output),
                  signing_certificate_sha256=cert.group(1).lower())
    output.with_suffix('.json').write_text(json.dumps(signed, indent=2, sort_keys=True) + '\n')
    return signed


class AmazonAPI:
    def __init__(self, app_id, client_id, client_secret, opener=urllib.request.urlopen):
        if not app_id or not client_id or not client_secret:
            raise ValueError('Amazon app ID and API credentials are required')
        self.root = API + '/' + urllib.parse.quote(app_id, safe='')
        self.opener = opener
        data = urllib.parse.urlencode({'client_id': client_id, 'client_secret': client_secret,
            'grant_type': 'client_credentials', 'scope': 'appstore::apps:readwrite'}).encode()
        response, _ = self.request('POST', TOKEN, data, {'Content-Type': 'application/x-www-form-urlencoded'}, auth=False)
        self.token = response['access_token']

    def request(self, method, path, data=None, headers=None, auth=True):
        url = path if path.startswith('https://') else self.root + path
        header = dict(headers or {})
        if auth: header['Authorization'] = 'Bearer ' + self.token
        request = urllib.request.Request(url, data=data, headers=header, method=method)
        try:
            with self.opener(request, timeout=180) as response:
                body = response.read()
                return (json.loads(body) if body else None), response.headers.get('ETag')
        except urllib.error.HTTPError as error:
            # Never include bearer tokens or presigned URLs in workflow logs.
            raise ValueError(f'Amazon API {method} {urllib.parse.urlsplit(url).path} returned HTTP {error.code}') from None


def publish(api, apk, manifest, sdk, notes):
    from developer_apk import build_tools, java_environment
    metadata = json.loads(manifest.read_text())
    version = metadata.get('version_name', '')
    if (not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version)
            or metadata.get('sha256') != sha256(apk)
            or not re.fullmatch(r'[0-9a-f]{64}', metadata.get('unsigned_sha256', ''))
            or metadata.get('version_code') != version_code(version)
            or metadata.get('package') != PACKAGE or tuple(metadata.get('architectures', [])) != ABIS
            or metadata.get('tag') != 'v' + version
            or not re.fullmatch(r'[0-9a-f]{40}', metadata.get('source_commit', ''))
            or not re.fullmatch(r'[0-9a-f]{64}', metadata.get('signing_certificate_sha256', ''))):
        raise ValueError('Signed Amazon artifact provenance mismatch')
    verify_apk(apk, sdk, version)
    cert_output = subprocess.check_output([str(build_tools(ROOT, sdk) / 'apksigner'), 'verify',
        '--print-certs', str(apk)], env=java_environment(ROOT), text=True)
    if metadata.get('signing_certificate_sha256') not in cert_output.lower():
        raise ValueError('Amazon APK signing certificate changed')
    if not notes.strip():
        raise ValueError('Release notes are required')
    if apk.stat().st_size > 300 * 1024 * 1024:
        raise ValueError('APK exceeds the simple-upload limit; use Amazon large-APK upload')
    active, _ = api.request('GET', '/edits')
    if active and active.get('id'):
        raise ValueError('Amazon already has an open edit; reconcile it in the console before publishing')
    edit, _ = api.request('POST', '/edits', b'')
    if not edit or not edit.get('id'):
        raise ValueError('Amazon did not return an edit ID')
    edit_id = urllib.parse.quote(str(edit['id']), safe='')
    base = '/edits/' + edit_id
    try:
        apks, _ = api.request('GET', base + '/apks')
        if not isinstance(apks, list) or len(apks) != 1:
            raise ValueError('Expected exactly one existing APK; inspect Amazon device targeting')
        old = apks[0]
        if int(old['versionCode']) >= metadata['version_code']:
            raise ValueError('Amazon versionCode must exceed the live version')
        apk_id = urllib.parse.quote(str(old['id']), safe='')
        _, etag = api.request('GET', base + '/apks/' + apk_id)
        if not etag:
            raise ValueError('Amazon did not return an APK ETag')
        uploaded, _ = api.request('PUT', base + '/apks/' + apk_id + '/replace', apk.read_bytes(),
            {'Content-Type': 'application/octet-stream', 'If-Match': etag, 'fileName': apk.name})
        if not uploaded or int(uploaded.get('versionCode', -1)) != metadata['version_code']:
            raise ValueError('Amazon returned a different APK version')
        listing, etag = api.request('GET', base + '/listings/en_US')
        if not isinstance(listing, dict) or not etag:
            raise ValueError('Amazon English listing or ETag is missing')
        listing['recentChanges'] = notes.strip()
        api.request('PUT', base + '/listings/en_US', json.dumps(listing).encode(),
                    {'Content-Type': 'application/json', 'If-Match': etag})
        api.request('POST', base + '/validate', b'')
        _, etag = api.request('GET', base)
        if not etag:
            raise ValueError('Amazon did not return an edit ETag')
        submitted, _ = api.request('POST', base + '/commit', b'', {'If-Match': etag})
        return {'edit_id': edit['id'], 'submission': submitted, 'sha256': metadata['sha256'],
                'version_code': metadata['version_code'], 'source_commit': metadata['source_commit']}
    except Exception as error:
        raise ValueError(f'Amazon edit {edit["id"]} remains open: {error}') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('version-code')
    for name in ('prepare', 'verify', 'sign', 'publish'):
        command = commands.add_parser(name)
        command.add_argument('--apk', type=Path, required=True)
        command.add_argument('--android-sdk', type=Path, default=ROOT / 'build/mobile-tools/android-sdk')
        if name != 'prepare': command.add_argument('--manifest', type=Path, required=True)
        if name == 'prepare':
            command.add_argument('--source-commit', required=True)
            command.add_argument('--tag', required=True)
            command.add_argument('--output', type=Path, required=True)
        if name == 'sign':
            command.add_argument('--keystore', type=Path, required=True)
            command.add_argument('--key-alias', required=True)
            command.add_argument('--expected-cert-sha256', required=True)
            command.add_argument('--output', type=Path, required=True)
        if name == 'publish': command.add_argument('--release-notes', required=True)
    args = parser.parse_args()
    if args.command == 'version-code': print(version_code()); return
    if args.command == 'prepare': result = prepare(args.apk, args.android_sdk, args.source_commit, args.tag, args.output)
    elif args.command == 'verify': result = verify_artifact(args.apk, args.manifest, args.android_sdk)
    elif args.command == 'sign': result = sign(args.apk, args.manifest, args.android_sdk, args.keystore,
        args.key_alias, args.output, args.expected_cert_sha256)
    else:
        api = AmazonAPI(os.environ.get('GLOB2_AMAZON_APP_ID'), os.environ.get('GLOB2_AMAZON_CLIENT_ID'),
                        os.environ.get('GLOB2_AMAZON_CLIENT_SECRET'))
        result = publish(api, args.apk, args.manifest, args.android_sdk, args.release_notes)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
