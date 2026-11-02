#!/usr/bin/env python3
"""Replace one explicitly selected older manual iOS review with the tagged build.

Withdrawal removes the older version from Apple's queue; submission of the new
version remains a separate protected action.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import time
from urllib.parse import urlencode
from ios_store_release import BUNDLE, Client, jwt_token, one, relation, resource, state, validate_provenance


def supersede(api, provenance, version_id, expected_build_id, pause=time.sleep):
    if any(not re.fullmatch('[A-Za-z0-9-]{1,128}', value) for value in (version_id, expected_build_id)):
        raise ValueError('Explicit version and current build IDs are required')
    app = one(api.collect('/v1/apps?' + urlencode({'filter[bundleId]': BUNDLE})), 'Globulation 2 app')
    build = one(api.collect('/v1/builds?' + urlencode({'filter[app]': app['id'],
        'filter[version]': provenance['buildNumber'],
        'filter[preReleaseVersion.version]': provenance['marketingVersion'],
        'filter[preReleaseVersion.platform]': 'IOS'})), 'uploaded tagged iOS build')
    attrs = build['attributes']
    if (attrs.get('processingState') != 'VALID' or attrs.get('expired') is not False or
            attrs.get('buildAudienceType') != 'APP_STORE_ELIGIBLE' or
            attrs.get('usesNonExemptEncryption') is not False):
        raise ValueError('Replacement build is not processed and eligible')
    versions = api.collect(f"/v1/apps/{app['id']}/appStoreVersions?" + urlencode({'filter[platform]': 'IOS'}))
    version = one([item for item in versions if item['id'] == version_id], 'explicit version belonging to this app')
    path = f'/v1/appStoreVersions/{version_id}'
    attached = api.request('GET', path + '/relationships/build').get('data')
    if (version['attributes'].get('platform') != 'IOS' or
            version['attributes'].get('releaseType') != 'MANUAL' or not attached):
        raise ValueError('Replacement requires a manual iOS version with an attached build')
    if (version['attributes'].get('versionString') == provenance['marketingVersion'] and attached['id'] == build['id']):
        return {'versionId': version_id, 'buildId': build['id'], 'state': state(version), 'alreadyPrepared': True}
    if attached['id'] != expected_build_id:
        raise ValueError('Current build changed; refusing to supersede another build')
    previous = version['attributes']['versionString']
    if tuple(map(int, previous.split('.'))) >= tuple(map(int, provenance['marketingVersion'].split('.'))):
        raise ValueError('Only an older version may be superseded')
    if state(version) not in ('WAITING_FOR_REVIEW', 'DEVELOPER_REJECTED', 'PREPARE_FOR_SUBMISSION'):
        raise ValueError('Version is no longer waiting for review or editable')
    if state(version) == 'WAITING_FOR_REVIEW':
        submissions = api.collect(f"/v1/apps/{app['id']}/reviewSubmissions?" + urlencode({'filter[platform]': 'IOS'}))
        matching = []
        for submission in submissions:
            if submission['attributes']['state'] != 'WAITING_FOR_REVIEW':
                continue
            items = api.collect(f"/v1/reviewSubmissions/{submission['id']}/items?include=appStoreVersion")
            ids = [(item.get('relationships', {}).get('appStoreVersion', {}).get('data') or {}).get('id') for item in items]
            if version_id in ids:
                if ids != [version_id]:
                    raise ValueError('Refusing to withdraw a submission containing other items')
                matching.append(submission)
        submission = one(matching, 'waiting submission containing only the selected version')
        api.request('PATCH', f"/v1/reviewSubmissions/{submission['id']}",
                    resource('reviewSubmissions', submission['id'], {'canceled': True}))
        for attempt in range(12):
            version = api.request('GET', path)['data']
            if state(version) in ('DEVELOPER_REJECTED', 'PREPARE_FOR_SUBMISSION'):
                break
            pause(5)
        else:
            raise ValueError('Apple has not finished withdrawing the selected review; retry later')
        current = api.request('GET', path + '/relationships/build').get('data')
        if not current or current['id'] != expected_build_id or version['attributes'].get('releaseType') != 'MANUAL':
            raise ValueError('Version/build changed during withdrawal; refusing replacement')
    api.request('PATCH', path, resource('appStoreVersions', version_id,
        {'versionString': provenance['marketingVersion'], 'releaseType': 'MANUAL'},
        {'build': relation('builds', build['id'])}))
    updated = api.request('GET', path)['data']
    attached = api.request('GET', path + '/relationships/build').get('data')
    if (updated['attributes'].get('versionString') != provenance['marketingVersion'] or
            updated['attributes'].get('releaseType') != 'MANUAL' or not attached or attached['id'] != build['id']):
        raise ValueError('Apple did not confirm the selected replacement')
    return {'versionId': version_id, 'buildId': build['id'], 'state': state(updated), 'previousVersion': previous}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, required=True)
    for name in ('tag', 'source-commit', 'run-id', 'draft-version-id', 'replace-draft-build-id'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    record = validate_provenance(json.loads(args.provenance.read_text()), args.tag, args.source_commit, args.run_id)
    token = jwt_token(base64.b64decode(os.environ['IOS_ASC_KEY_P8_BASE64'], validate=True),
                      os.environ['IOS_ASC_KEY_ID'], os.environ['IOS_ASC_ISSUER_ID'])
    print(json.dumps(supersede(Client(token), record, args.draft_version_id, args.replace_draft_build_id), sort_keys=True))


if __name__ == '__main__':
    main()
