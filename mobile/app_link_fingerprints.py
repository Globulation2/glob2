#!/usr/bin/env python3
"""Report the public signing identities that invite-link verification needs.

Run by `.github/workflows/app-signing-fingerprints.yml` in the release mirror.
It prints certificate SHA-256 fingerprints, the Apple Team ID and the App ID's
capabilities. It never prints keys, passwords, keystore contents or tokens.
"""

import argparse
import base64
import binascii
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scons'))
sys.path.insert(0, str(ROOT / 'mobile'))

PACKAGE = 'org.globulation2.glob2'
BUNDLE_ID = 'org.globulation2.glob2'
TRACK = 'internal'
PLAY_API = 'https://androidpublisher.googleapis.com/androidpublisher/v3/applications/' + PACKAGE
ASC_API = 'https://api.appstoreconnect.apple.com/v1'
ASSOCIATED_DOMAINS = 'ASSOCIATED_DOMAINS'
ASSOCIATED_DOMAINS_ENTITLEMENT = 'com.apple.developer.associated-domains'
MULTICAST_ENTITLEMENT = 'com.apple.developer.networking.multicast'
FINGERPRINT = re.compile(r'^([0-9A-F]{2}:){31}[0-9A-F]{2}$')


def fingerprint(value):
    """A SHA-256 as the uppercase colon form `instance.yaml` expects.

    Accepts hex with or without colons, or base64 (either alphabet)."""
    text = value.strip()
    plain = text.replace(':', '')
    raw = b''
    if re.fullmatch(r'[0-9A-Fa-f]{64}', plain):
        raw = bytes.fromhex(plain)
    elif re.fullmatch(r'[A-Za-z0-9+/_-]{43}=?', text):
        raw = base64.urlsafe_b64decode(text.rstrip('=').replace('+', '-').replace('/', '_') + '=')
    if len(raw) != 32:
        raise ValueError('Not a SHA-256 certificate fingerprint')
    return ':'.join(f'{byte:02X}' for byte in raw)


def keytool_fingerprint(output):
    """The single certificate SHA-256 line from `keytool -list` output."""
    # Default listing: "Certificate fingerprint (SHA-256): AA:..."; -v listing: "SHA256: AA:...".
    found = re.findall(r'Certificate fingerprint \(SHA-256\):\s*([0-9A-Fa-f:]{95})', output)
    found += re.findall(r'^\s*SHA256:\s*([0-9A-Fa-f:]{95})\s*$', output, re.M)
    values = sorted({fingerprint(value) for value in found})
    if len(values) != 1:
        raise ValueError('Expected exactly one certificate SHA-256 in keytool output')
    return values[0]


def apksigner_fingerprints(output):
    """Signer certificate SHA-256 values from `apksigner verify --print-certs`."""
    found = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]{64})', output)
    if not found:
        raise ValueError('apksigner reported no signer certificate')
    return sorted({fingerprint(value) for value in found})


def java_tool(name):
    from developer_apk import java_environment
    env = java_environment(ROOT)
    home = env.get('JAVA_HOME')
    tool = Path(home) / 'bin' / name if home else shutil.which(name)
    if not tool or not Path(tool).is_file():
        raise ValueError(f'{name} is not installed')
    return str(tool), env


def keystore_fingerprint(encoded_env, password_env, alias):
    """Decode a base64 keystore secret into a private temp dir, read its
    certificate SHA-256, then overwrite and remove it."""
    encoded = ''.join(os.environ.get(encoded_env, '').split())
    if not encoded:
        raise ValueError(f'{encoded_env} is missing')
    if not os.environ.get(password_env):
        raise ValueError(f'{password_env} is missing')
    keytool, env = java_tool('keytool')
    directory = Path(tempfile.mkdtemp(prefix='glob2-keystore-', dir=os.environ.get('RUNNER_TEMP')))
    path = directory / 'release.p12'
    try:
        directory.chmod(0o700)
        path.write_bytes(base64.b64decode(encoded, validate=True))
        path.chmod(0o600)
        result = subprocess.run([keytool, '-list', '-keystore', str(path), '-storetype', 'PKCS12',
                                 '-alias', alias, '-storepass:env', password_env],
                                env=env, text=True, capture_output=True)
        if result.returncode:
            # keytool's messages name the problem without echoing secrets.
            raise ValueError('keytool could not read the keystore: ' + result.stdout.strip()[-200:])
        return keytool_fingerprint(result.stdout)
    finally:
        if path.exists():
            size = path.stat().st_size
            if shutil.which('shred'):
                subprocess.run(['shred', '-u', '-z', str(path)], check=False)
            else:
                with open(path, 'r+b') as handle:
                    handle.write(b'\0' * size)
                path.unlink(missing_ok=True)
        shutil.rmtree(directory, ignore_errors=True)


# Google Play ---------------------------------------------------------------

def latest_version_code(track):
    codes = [int(code) for release in track.get('releases', [])
             if release.get('status') in ('completed', 'inProgress', 'halted', 'draft')
             for code in release.get('versionCodes', [])]
    if not codes:
        raise ValueError(f'The {TRACK} track has no release')
    return max(codes)


def download_choice(entry):
    """The smallest likely APK signed with this key: a config split first."""
    splits = entry.get('generatedSplitApks', [])
    for split in sorted(splits, key=lambda item: (not item.get('splitId'), item.get('splitId', ''))):
        if split.get('downloadId'):
            return split['downloadId']
    for apk in entry.get('generatedStandaloneApks', []):
        if apk.get('downloadId'):
            return apk['downloadId']
    universal = entry.get('generatedUniversalApk') or {}
    if universal.get('downloadId'):
        return universal['downloadId']
    raise ValueError('Play listed no downloadable generated APK')


class Play:
    def __init__(self, session):
        self.session = session

    def call(self, method, path, **kwargs):
        response = self.session.request(method, PLAY_API + path, timeout=120, **kwargs)
        if response.status_code >= 400:
            raise PlayError(method, path, response.status_code, response.text[:300])
        return response

    def internal_version_code(self):
        edit = self.call('POST', '/edits', json={}).json()['id']
        try:
            return latest_version_code(self.call('GET', f'/edits/{edit}/tracks/{TRACK}').json())
        finally:
            # Read-only use: never commit the edit.
            try:
                self.call('DELETE', f'/edits/{edit}')
            except PlayError:
                pass

    def download(self, path, destination):
        response = self.call('GET', path, params={'alt': 'media'}, stream=True)
        with open(destination, 'wb') as output:
            for chunk in response.iter_content(1 << 20):
                output.write(chunk)

    def generated_apk(self, code, destination):
        """Play App Signing certificate from generatedapks.list/download."""
        entries = self.call('GET', f'/generatedApks/{code}').json().get('generatedApks', [])
        if not entries:
            raise ValueError(f'Play listed no generated APKs for version code {code}')
        listed, notes = set(), []
        for entry in entries:
            value = entry.get('certificateSha256Hash', '')
            try:
                listed.add(fingerprint(value))
            except ValueError:
                notes.append(f'unrecognised certificateSha256Hash {value!r}')
        download_id = download_choice(entries[0])
        self.download(f'/generatedApks/{code}/downloads/{urllib.parse.quote(download_id, safe="")}:download',
                      destination)
        return sorted(listed), 'generatedapks.list + generatedapks.download', notes

    def system_apk(self, code, destination):
        """Fallback: Play signs system APK variants with the app signing key too."""
        variants = self.call('GET', f'/systemApks/{code}/variants').json().get('variants', [])
        if not variants:
            spec = {'deviceSpec': {'supportedAbis': ['arm64-v8a'], 'supportedLocales': ['en-US'],
                                   'screenDensity': 420}}
            variants = [self.call('POST', f'/systemApks/{code}/variants', json=spec).json()]
        variant = variants[0]['variantId']
        self.download(f'/systemApks/{code}/variants/{variant}:download', destination)
        return [], 'systemapks.variants (fallback)', []


class PlayError(Exception):
    def __init__(self, method, path, status, body):
        super().__init__(f'{method} {path} -> HTTP {status}: {body}')
        self.status = status


def apk_signers(apk):
    from dev_store import android_sdk
    from developer_apk import build_tools, java_environment
    signer = build_tools(ROOT, android_sdk(ROOT)) / 'apksigner'
    output = subprocess.check_output([str(signer), 'verify', '--print-certs', str(apk)],
                                     env=java_environment(ROOT), text=True)
    return apksigner_fingerprints(output)


def play_signing(session, signers=apk_signers):
    play = Play(session)
    code = play.internal_version_code()
    notes = []
    with tempfile.TemporaryDirectory(prefix='glob2-play-apk-', dir=os.environ.get('RUNNER_TEMP')) as directory:
        apk = Path(directory) / 'play.apk'
        try:
            listed, method, extra = play.generated_apk(code, apk)
        except (PlayError, ValueError) as error:
            notes.append(f'generatedapks failed: {error}')
            listed, method, extra = play.system_apk(code, apk)
        notes += extra
        signed = signers(apk)
    if listed and listed != signed:
        raise ValueError(f'Play listed {listed} but the downloaded APK is signed by {signed}')
    return {'version_code': code, 'fingerprints': signed, 'method': method, 'notes': notes}


# App Store Connect ---------------------------------------------------------

def der_to_raw_signature(der, size=32):
    """ECDSA DER SEQUENCE{r, s} to the JOSE r||s form."""
    if len(der) < 8 or der[0] != 0x30:
        raise ValueError('Not a DER ECDSA signature')
    index = 2 if der[1] < 0x80 else 2 + (der[1] & 0x7f)
    parts = []
    for _ in range(2):
        if der[index] != 0x02:
            raise ValueError('Malformed DER ECDSA signature')
        length = der[index + 1]
        value = der[index + 2:index + 2 + length].lstrip(b'\0')
        if len(value) > size:
            raise ValueError('ECDSA integer too large')
        parts.append(value.rjust(size, b'\0'))
        index += 2 + length
    return b''.join(parts)


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def asc_token(key_id, issuer, key_path, now=None):
    now = int(time.time() if now is None else now)
    header = b64url(json.dumps({'alg': 'ES256', 'kid': key_id, 'typ': 'JWT'}).encode())
    claims = b64url(json.dumps({'iss': issuer, 'iat': now, 'exp': now + 900,
                                'aud': 'appstoreconnect-v1'}).encode())
    message = f'{header}.{claims}'.encode()
    der = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key_path)], input=message,
                         capture_output=True, check=True).stdout
    return f'{header}.{claims}.{b64url(der_to_raw_signature(der))}'


class AppStoreConnect:
    def __init__(self, token, opener=urllib.request.urlopen):
        self.token = token
        self.opener = opener

    def call(self, method, path, body=None):
        url = path if path.startswith('https://') else ASC_API + path
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(url, data=data, method=method, headers={
            'Authorization': 'Bearer ' + self.token, 'Content-Type': 'application/json'})
        try:
            with self.opener(request, timeout=60) as response:
                text = response.read()
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors='replace')[:400]
            raise ValueError(f'App Store Connect {method} {urllib.parse.urlsplit(url).path} '
                             f'-> HTTP {error.code}: {detail}') from None
        return json.loads(text) if text else {}

    def pages(self, path):
        while path:
            page = self.call('GET', path)
            yield from page.get('data', [])
            path = page.get('links', {}).get('next')


def profile_entitlements(content):
    """Entitlements of a base64 provisioning profile (CMS wrapping a plist)."""
    raw = base64.b64decode(content)
    start, end = raw.find(b'<?xml'), raw.rfind(b'</plist>')
    if start < 0 or end < 0:
        raise ValueError('Provisioning profile has no property list')
    return plistlib.loads(raw[start:end + len(b'</plist>')])


def ios_app_id(asc, enable=True):
    query = urllib.parse.urlencode({'filter[identifier]': BUNDLE_ID, 'limit': 200})
    matches = [item for item in asc.pages('/bundleIds?' + query)
               if item['attributes'].get('identifier') == BUNDLE_ID]
    if len(matches) != 1:
        raise ValueError(f'Expected one App ID {BUNDLE_ID}, found {len(matches)}')
    bundle = matches[0]
    capabilities = lambda: sorted(item['attributes']['capabilityType'] for item in
                                  asc.pages(f"/bundleIds/{bundle['id']}/bundleIdCapabilities?limit=200"))
    before = capabilities()
    enabled_now = False
    if ASSOCIATED_DOMAINS not in before and enable:
        asc.call('POST', '/bundleIdCapabilities', {'data': {
            'type': 'bundleIdCapabilities', 'attributes': {'capabilityType': ASSOCIATED_DOMAINS},
            'relationships': {'bundleId': {'data': {'type': 'bundleIds', 'id': bundle['id']}}}}})
        enabled_now = True
    after = capabilities() if enabled_now else before
    profiles = []
    for item in asc.pages(f"/bundleIds/{bundle['id']}/profiles?limit=200"):
        attributes = item['attributes']
        row = {key: attributes.get(key) for key in ('name', 'profileType', 'profileState', 'expirationDate')}
        try:
            plist = profile_entitlements(attributes['profileContent'])
            entitlements = plist.get('Entitlements', {})
            row['team'] = (plist.get('TeamIdentifier') or [None])[0]
            row['associated_domains'] = ASSOCIATED_DOMAINS_ENTITLEMENT in entitlements
            row['multicast'] = MULTICAST_ENTITLEMENT in entitlements
        except (KeyError, ValueError, plistlib.InvalidFileException, binascii.Error):
            row['associated_domains'] = row['multicast'] = None
        profiles.append(row)
    return {'bundle_id': BUNDLE_ID, 'team_id': bundle['attributes'].get('seedId'),
            'name': bundle['attributes'].get('name'), 'platform': bundle['attributes'].get('platform'),
            'capabilities': after, 'enabled_associated_domains': enabled_now,
            'has_associated_domains': ASSOCIATED_DOMAINS in after, 'profiles': profiles}


# Reporting -----------------------------------------------------------------

def instance_yaml(fingerprints, team_id):
    lines = ['appLinks:']
    values = [value for value in fingerprints if value]
    if values:
        lines += ['  android:', f'    packageName: {PACKAGE}', '    sha256CertFingerprints:']
        lines += [f'      - {value}' for value in dict.fromkeys(values)]
    if team_id:
        lines += ['  ios:', f'    appIds: [{team_id}.{BUNDLE_ID}]']
    return '\n'.join(lines) + '\n'


def write_summary(text):
    print(text)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a') as output:
            output.write(text + '\n')


def write_outputs(**values):
    target = os.environ.get('GITHUB_OUTPUT')
    if target:
        with open(target, 'a') as output:
            for key, value in values.items():
                if '\n' in str(value):
                    raise ValueError('Single-line outputs only')
                output.write(f'{key}={value}\n')


def ios_report(result):
    rows = '\n'.join(
        f"| {p['name']} | {p['profileType']} | {p['profileState']} | {p['expirationDate']} | "
        f"{'yes' if p['associated_domains'] else 'no' if p['associated_domains'] is False else '?'} | "
        f"{'yes' if p['multicast'] else 'no' if p['multicast'] is False else '?'} |"
        for p in result['profiles']) or '| (none) | | | | | |'
    state = ('enabled by this run' if result['enabled_associated_domains'] else
             'already enabled' if result['has_associated_domains'] else 'NOT enabled')
    return (f"### iOS App ID `{result['bundle_id']}`\n\n"
            f"- Team ID (App ID prefix): `{result['team_id']}`\n"
            f"- Associated Domains: {state}\n"
            f"- Capabilities: {', '.join(result['capabilities']) or '(none)'}\n\n"
            "| Profile | Type | State | Expires | Associated domains | Multicast |\n"
            "| --- | --- | --- | --- | --- | --- |\n" + rows + '\n\n'
            'Enabling a capability invalidates existing profiles; the next TestFlight export '
            '(`-allowProvisioningUpdates`) regenerates the Xcode-managed App Store profile.\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    keystore = commands.add_parser('keystore', help='Certificate SHA-256 of a base64 keystore secret')
    keystore.add_argument('--label', required=True)
    keystore.add_argument('--keystore-env', required=True)
    keystore.add_argument('--password-env', required=True)
    keystore.add_argument('--alias', required=True)
    keystore.add_argument('--expected', default='')
    keystore.add_argument('--output', required=True)
    commands.add_parser('play', help='Play App Signing certificate of the latest internal release')
    ios = commands.add_parser('ios', help='Check and enable Associated Domains on the App ID')
    ios.add_argument('--key-path', type=Path, required=True)
    ios.add_argument('--no-enable', action='store_true')
    report = commands.add_parser('report', help='Write the instance.yaml snippet')
    report.add_argument('--fingerprint', action='append', default=[])
    report.add_argument('--team-id', default='')
    args = parser.parse_args()

    if args.command == 'keystore':
        value = keystore_fingerprint(args.keystore_env, args.password_env, args.alias)
        line = f'### {args.label}\n\nCertificate SHA-256: `{value}`\n'
        if args.expected:
            match = fingerprint(args.expected) == value
            line += f"\nMatches the configured value: {'yes' if match else '**NO**'}\n"
        write_summary(line)
        write_outputs(**{args.output: value})
    elif args.command == 'play':
        import google.auth
        from google.auth.transport.requests import AuthorizedSession
        credentials, _ = google.auth.default(scopes=['https://www.googleapis.com/auth/androidpublisher'])
        result = play_signing(AuthorizedSession(credentials))
        notes = ''.join(f'\n- {note}' for note in result['notes'])
        write_summary(f"### Google Play App Signing certificate\n\n"
                      f"Read from version code {result['version_code']} on the {TRACK} track via "
                      f"{result['method']}; the downloaded APK's signer, checked with apksigner:\n\n"
                      + ''.join(f'- `{value}`\n' for value in result['fingerprints']) + notes)
        write_outputs(play_sha256=','.join(result['fingerprints']))
    elif args.command == 'ios':
        token = asc_token(os.environ['ASC_KEY_ID'], os.environ['ASC_ISSUER_ID'], args.key_path)
        result = ios_app_id(AppStoreConnect(token), enable=not args.no_enable)
        write_summary(ios_report(result))
        write_outputs(team_id=result['team_id'] or '',
                      associated_domains=str(result['has_associated_domains']).lower())
    else:
        values = [fingerprint(value) for item in args.fingerprint for value in item.split(',') if value.strip()]
        if args.team_id and not re.fullmatch(r'[A-Z0-9]{10}', args.team_id):
            raise ValueError('Unexpected Team ID')
        write_summary('### instance.yaml\n\nPaste into `/opt/glob2/config/instance.yaml`, then '
                      '`docker compose up -d --force-recreate platform-api platform-worker` in the deployment directory:\n\n'
                      '```yaml\n' + instance_yaml(values, args.team_id) + '```\n')


if __name__ == '__main__':
    main()
