#!/usr/bin/env python3
"""Publish a signed Globulation 2 bundle to Google Play internal testing."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import time


PACKAGE = 'org.globulation2.glob2'
TRACK = 'internal'
VERSION_EPOCH = 1704067200  # 2024-01-01 UTC; leaves room under Play's 2.1B limit.
MAX_VERSION_CODE = 2100000000


def version_code(now=None):
    code = int(time.time() if now is None else now) - VERSION_EPOCH
    if not 1 < code <= MAX_VERSION_CODE:
        raise ValueError('Generated Play version code is outside the supported range')
    return code


def bundle_metadata(bundle, code):
    if not bundle.is_file() or bundle.name != 'app-release-play.aab':
        raise ValueError('Expected the signed Play upload bundle')
    unsigned = bundle.with_name('app-release.aab')
    metadata = unsigned.with_suffix('.json')
    if not unsigned.is_file() or not metadata.is_file():
        raise ValueError('Verified unsigned bundle metadata is missing')
    provenance = json.loads(metadata.read_text())
    if provenance.get('version_code') != code or provenance.get('architecture') != 'arm64-v8a':
        raise ValueError('Bundle version or architecture differs from the release')
    if provenance.get('sha256') != hashlib.sha256(unsigned.read_bytes()).hexdigest():
        raise ValueError('Unsigned bundle changed after verification')
    return hashlib.sha256(bundle.read_bytes()).hexdigest()


def release_body(code, name, notes):
    if not name or len(name) > 50 or not notes.strip():
        raise ValueError('Release name and notes are required')
    return {'track': TRACK, 'releases': [{
        'name': name,
        'versionCodes': [str(code)],
        'status': 'completed',
        'releaseNotes': [{'language': 'en-US', 'text': notes.strip()}],
    }]}


def publish(service, bundle, code, name, notes):
    """Create a Play edit, upload the bundle, and commit only the internal track."""
    from googleapiclient.http import MediaFileUpload

    digest = bundle_metadata(bundle, code)
    edits = service.edits()
    edit = edits.insert(packageName=PACKAGE, body={}).execute(num_retries=3)
    edit_id = edit['id']
    args = {'packageName': PACKAGE, 'editId': edit_id}
    current = edits.tracks().get(track=TRACK, **args).execute(num_retries=3)
    active_codes = [int(value) for release in current.get('releases', [])
                    for value in release.get('versionCodes', [])]
    if active_codes and code <= max(active_codes):
        raise ValueError('Version code must exceed the current internal release')

    media = MediaFileUpload(str(bundle), mimetype='application/octet-stream',
                            chunksize=8 * 1024 * 1024, resumable=True)
    uploaded = edits.bundles().upload(media_body=media, **args).execute(num_retries=5)
    if int(uploaded['versionCode']) != code or uploaded.get('sha256', '').lower() != digest:
        raise ValueError('Play returned a different bundle version or SHA-256; edit was not committed')

    desired = release_body(code, name, notes)
    updated = edits.tracks().update(track=TRACK, body=desired, **args).execute(num_retries=3)
    if updated.get('track') != TRACK or not any(
            release.get('status') == 'completed' and str(code) in release.get('versionCodes', [])
            for release in updated.get('releases', [])):
        raise ValueError('Play did not confirm the internal track update; edit was not committed')
    edits.validate(**args).execute(num_retries=3)
    edits.commit(**args).execute(num_retries=3)
    return {'package': PACKAGE, 'track': TRACK, 'version_code': code,
            'bundle_sha256': digest, 'release_name': name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest='command', required=True)
    subcommands.add_parser('version-code')
    release = subcommands.add_parser('publish')
    release.add_argument('--bundle', type=Path, required=True)
    release.add_argument('--version-code', type=int, required=True)
    release.add_argument('--release-notes', required=True)
    args = parser.parse_args()
    if args.command == 'version-code':
        print(version_code())
        return
    if not 1 < args.version_code <= MAX_VERSION_CODE:
        raise ValueError('Invalid Play version code')
    from googleapiclient.discovery import build
    import google.auth

    credentials, _ = google.auth.default(scopes=['https://www.googleapis.com/auth/androidpublisher'])
    service = build('androidpublisher', 'v3', credentials=credentials, cache_discovery=False)
    revision = os.environ.get('GITHUB_SHA', 'local')[:7]
    name = f'Internal {args.version_code} ({revision})'
    result = publish(service, args.bundle.resolve(), args.version_code, name, args.release_notes)
    print(json.dumps(result, sort_keys=True))
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a') as output:
            output.write(f"### Google Play internal release\n\n"
                         f"{name} · version code {args.version_code} · `{result['bundle_sha256']}`\n\n"
                         f"[Open the internal testing track](https://play.google.com/console/u/0/developers/7470334415218720552/app/4974067701875576344/tracks/4700775093912551215)\n")


if __name__ == '__main__':
    main()
