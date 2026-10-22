#!/usr/bin/env python3
"""Read iOS version identities and states without changing App Store records."""
import argparse
import base64
import json
import os
from pathlib import Path
from urllib.parse import urlencode
from ios_store_release import BUNDLE, Client, jwt_token, one, state, validate_provenance


def inspect(api, provenance):
    app = one(api.collect('/v1/apps?' + urlencode({'filter[bundleId]': BUNDLE})),
              'Globulation 2 app')
    versions = api.collect(f"/v1/apps/{app['id']}/appStoreVersions?" +
                           urlencode({'filter[platform]': 'IOS'}))
    result = []
    for version in versions:
        build = api.request('GET', f"/v1/appStoreVersions/{version['id']}/relationships/build").get('data')
        result.append({'versionId': version['id'],
                       'version': version['attributes'].get('versionString'),
                       'state': state(version),
                       'releaseType': version['attributes'].get('releaseType'),
                       'buildId': build['id'] if build else None})
    return {'appId': app['id'], 'selectedMarketingVersion': provenance['marketingVersion'],
            'selectedBuildNumber': provenance['buildNumber'], 'versions': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, required=True)
    for name in ('tag', 'source-commit', 'run-id'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    record = validate_provenance(json.loads(args.provenance.read_text()),
                                 args.tag, args.source_commit, args.run_id)
    token = jwt_token(base64.b64decode(os.environ['IOS_ASC_KEY_P8_BASE64'], validate=True),
                      os.environ['IOS_ASC_KEY_ID'], os.environ['IOS_ASC_ISSUER_ID'])
    print(json.dumps(inspect(Client(token), record), sort_keys=True))


if __name__ == '__main__':
    main()
