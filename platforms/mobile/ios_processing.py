#!/usr/bin/env python3
"""Wait for Apple's processing of one exact uploaded build before attesting it."""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import time
from urllib.parse import urlencode

from ios_store_release import BUNDLE, Client, one


def jwt_token(key_path, key_id, issuer):
    def encode(value):
        return base64.urlsafe_b64encode(json.dumps(value, separators=(',', ':')).encode()).rstrip(b'=')
    now = int(time.time())
    payload = encode({'alg': 'ES256', 'kid': key_id, 'typ': 'JWT'}) + b'.' + encode({
        'iss': issuer, 'iat': now, 'exp': now + 600, 'aud': 'appstoreconnect-v1'})
    signed = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key_path)],
        input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if signed.returncode:
        raise ValueError('Unable to sign Apple authentication token with the configured key')
    der = signed.stdout
    if len(der) < 8 or der[0] != 48 or der[1] != len(der) - 2 or der[2] != 2:
        raise ValueError('Unexpected Apple P-256 signature encoding')
    r_length = der[3]
    offset = 4 + r_length
    if offset + 2 > len(der) or der[offset] != 2 or offset + 2 + der[offset + 1] != len(der):
        raise ValueError('Unexpected Apple P-256 signature integers')
    try:
        signature = int.from_bytes(der[4:offset]).to_bytes(32, 'big') + int.from_bytes(der[offset + 2:]).to_bytes(32, 'big')
    except OverflowError:
        raise ValueError('Apple authentication requires a P-256 key') from None
    return (payload + b'.' + base64.urlsafe_b64encode(signature).rstrip(b'=')).decode()


def wait_for_build(api_factory, build_number, marketing_version, audience,
                   timeout=1800, interval=30, clock=time.monotonic, sleep=time.sleep):
    if timeout <= 0 or interval <= 0:
        raise ValueError('Processing timeout and polling interval must be positive')
    app = one(api_factory().collect('/v1/apps?' + urlencode({'filter[bundleId]': BUNDLE})),
              'Globulation 2 app')
    path = '/v1/builds?' + urlencode({'filter[app]': app['id'], 'filter[version]': build_number,
        'filter[preReleaseVersion.version]': marketing_version,
        'filter[preReleaseVersion.platform]': 'IOS'})
    deadline = clock() + timeout
    while True:
        builds = api_factory().collect(path)
        if len(builds) > 1:
            raise ValueError('Apple returned multiple builds for the exact upload identity')
        if builds:
            build = builds[0]
            attrs = build['attributes']
            if attrs.get('version') != str(build_number):
                raise ValueError('Apple returned a different build number')
            status = attrs.get('processingState')
            if status == 'VALID':
                if attrs.get('expired') is not False:
                    raise ValueError('The processed Apple build is expired or expiry is unknown')
                if audience == 'external' and attrs.get('buildAudienceType') != 'APP_STORE_ELIGIBLE':
                    raise ValueError('The processed Apple build is not App Store eligible')
                if attrs.get('usesNonExemptEncryption') is not False:
                    raise ValueError('The processed Apple build has unresolved encryption metadata')
                return {'buildId': build['id'], 'buildNumber': str(build_number),
                        'marketingVersion': marketing_version, 'processingState': status,
                        'appStoreEligible': attrs.get('buildAudienceType') == 'APP_STORE_ELIGIBLE'}
            if status != 'PROCESSING':
                raise ValueError('Apple rejected processing or returned an unknown state: ' + str(status))
        remaining = deadline - clock()
        if remaining <= 0:
            raise ValueError('Apple did not return a valid processed build before the deadline; '
                             'check App Store Connect delivery errors and the Apple notification email')
        sleep(min(interval, remaining))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key-path', type=Path, required=True)
    for name in ('key-id', 'issuer-id', 'build-number', 'marketing-version'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--audience', choices=('internal', 'external'), required=True)
    parser.add_argument('--timeout', type=int, default=1800)
    args = parser.parse_args()
    # Refresh short-lived tokens for every observation; processing can exceed their lifetime.
    def api(): return Client(jwt_token(args.key_path, args.key_id, args.issuer_id))
    result = wait_for_build(api, args.build_number, args.marketing_version, args.audience,
                            timeout=args.timeout)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
