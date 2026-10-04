"""Signing-identity report for invite links: parsing, Play and App Store Connect flows."""
import base64
import io
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'mobile'))
import app_link_fingerprints as links

HEX = 'ab' * 32
COLON = ':'.join(['AB'] * 32)
WORKFLOW = ROOT / '.github/workflows/app-signing-fingerprints.yml'


class FingerprintTests(unittest.TestCase):
    def test_normalises_hex_colon_and_base64(self):
        self.assertEqual(links.fingerprint(HEX), COLON)
        self.assertEqual(links.fingerprint(COLON.lower()), COLON)
        raw = bytes(range(32))
        expected = ':'.join(f'{b:02X}' for b in raw)
        self.assertEqual(links.fingerprint(base64.b64encode(raw).decode()), expected)
        self.assertEqual(links.fingerprint(base64.urlsafe_b64encode(raw).decode().rstrip('=')), expected)
        self.assertRegex(links.fingerprint(HEX), links.FINGERPRINT)
        for bad in ('', 'ab' * 20, 'zz' * 32, COLON[:-3]):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                links.fingerprint(bad)

    def test_keytool_listing_yields_only_the_sha256(self):
        listing = ('Keystore type: PKCS12\n\nglob2-upload, Oct 1, 2026, PrivateKeyEntry, \n'
                   f'Certificate fingerprint (SHA-256): {COLON}\n')
        self.assertEqual(links.keytool_fingerprint(listing), COLON)
        verbose = f'Certificate fingerprints:\n\t SHA1: {":".join(["CD"] * 20)}\n\t SHA256: {COLON}\n'
        self.assertEqual(links.keytool_fingerprint(verbose), COLON)
        with self.assertRaises(ValueError):
            links.keytool_fingerprint('no certificate here')
        with self.assertRaises(ValueError):
            links.keytool_fingerprint(listing + f'Certificate fingerprint (SHA-256): {":".join(["CD"] * 32)}\n')

    def test_apksigner_output(self):
        text = f'Signer #1 certificate DN: CN=Android\nSigner #1 certificate SHA-256 digest: {HEX}\n'
        self.assertEqual(links.apksigner_fingerprints(text), [COLON])
        with self.assertRaises(ValueError):
            links.apksigner_fingerprints('DOES NOT VERIFY')

    def test_snippet_matches_instance_schema(self):
        text = links.instance_yaml([COLON, COLON, None], 'CL2MNNYQX3')
        self.assertEqual(text, 'appLinks:\n  android:\n    packageName: org.globulation2.glob2\n'
                               f'    sha256CertFingerprints:\n      - {COLON}\n'
                               '  ios:\n    appIds: [CL2MNNYQX3.org.globulation2.glob2]\n')
        self.assertNotIn('android', links.instance_yaml([], 'CL2MNNYQX3'))


def pinned_keytool():
    try:
        tool = Path(links.java_tool('keytool')[0])
        subprocess.run([str(tool), '-help'], check=True, capture_output=True)
        return tool
    except (ValueError, OSError, subprocess.CalledProcessError):
        return None


KEYTOOL = pinned_keytool()


@unittest.skipUnless(KEYTOOL and shutil.which('openssl'), 'needs the pinned JDK (mobile/setup_tools.py) and openssl')
class KeystoreTests(unittest.TestCase):
    def test_reads_certificate_and_removes_decoded_keystore(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Path(directory) / 'k.p12'
            subprocess.run([str(KEYTOOL), '-genkeypair', '-keystore', str(store), '-storetype', 'PKCS12',
                            '-alias', 'glob2-upload', '-keyalg', 'EC', '-dname', 'CN=Test', '-validity', '2',
                            '-storepass', 'secret-pass', '-keypass', 'secret-pass'], check=True, capture_output=True)
            der = subprocess.run([str(KEYTOOL), '-exportcert', '-keystore', str(store), '-alias', 'glob2-upload',
                                  '-storepass', 'secret-pass'], check=True, capture_output=True).stdout
            expected = links.fingerprint(subprocess.run(['openssl', 'dgst', '-sha256', '-hex'], input=der, check=True,
                                                        capture_output=True).stdout.decode().split()[-1])
            temp = Path(directory) / 'runner'
            temp.mkdir()
            env = {'KS': base64.b64encode(store.read_bytes()).decode(), 'PW': 'secret-pass', 'RUNNER_TEMP': str(temp)}
            with patch.dict(os.environ, env):
                self.assertEqual(links.keystore_fingerprint('KS', 'PW', 'glob2-upload'), expected)
                os.environ['PW'] = 'wrong-pass'
                with self.assertRaises(ValueError) as caught:
                    links.keystore_fingerprint('KS', 'PW', 'glob2-upload')
                self.assertNotIn('wrong-pass', str(caught.exception))
            self.assertEqual(list(temp.iterdir()), [])


class FakeResponse:
    def __init__(self, status=200, data=None, content=b''):
        self.status_code, self.data, self.content = status, data, content
        self.text = json.dumps(data) if data is not None else ''

    def json(self):
        return self.data

    def iter_content(self, size):
        yield self.content


class FakeSession:
    def __init__(self, routes):
        self.routes, self.calls = routes, []

    def request(self, method, url, **kwargs):
        path = url[len(links.PLAY_API):]
        self.calls.append((method, path))
        for (route_method, pattern), response in self.routes.items():
            if route_method == method and re.fullmatch(pattern, path):
                return response(kwargs) if callable(response) else response
        return FakeResponse(404, {'error': 'missing'})


class PlayTests(unittest.TestCase):
    def routes(self, generated):
        return {('POST', '/edits'): FakeResponse(data={'id': 'e1'}),
                ('GET', '/edits/e1/tracks/internal'): FakeResponse(data={'releases': [
                    {'status': 'completed', 'versionCodes': ['90']}, {'status': 'completed', 'versionCodes': ['120']}]}),
                ('DELETE', '/edits/e1'): FakeResponse(204),
                ('GET', '/generatedApks/120'): generated,
                ('GET', r'/generatedApks/120/downloads/.+:download'): FakeResponse(content=b'apk'),
                ('GET', '/systemApks/120/variants'): FakeResponse(data={}),
                ('POST', '/systemApks/120/variants'): FakeResponse(data={'variantId': 7}),
                ('GET', '/systemApks/120/variants/7:download'): FakeResponse(content=b'apk')}

    def test_reads_latest_internal_release_and_never_commits_the_edit(self):
        entry = {'certificateSha256Hash': base64.b64encode(bytes.fromhex(HEX)).decode(),
                 'generatedSplitApks': [{'moduleName': 'base', 'downloadId': 'base'},
                                        {'moduleName': 'base', 'splitId': 'config.en', 'downloadId': 'en'}],
                 'generatedUniversalApk': {'downloadId': 'universal'}}
        session = FakeSession(self.routes(FakeResponse(data={'generatedApks': [entry]})))
        seen = []
        result = links.play_signing(session, signers=lambda apk: seen.append(apk.read_bytes()) or [COLON])
        self.assertEqual(result['version_code'], 120)
        self.assertEqual(result['fingerprints'], [COLON])
        self.assertEqual(seen, [b'apk'])
        self.assertIn(('GET', '/generatedApks/120/downloads/en:download'), session.calls)
        self.assertIn(('DELETE', '/edits/e1'), session.calls)
        self.assertFalse(any(path.endswith(':commit') for _, path in session.calls))

    def test_mismatch_between_listing_and_apk_fails(self):
        entry = {'certificateSha256Hash': HEX, 'generatedUniversalApk': {'downloadId': 'u'}}
        session = FakeSession(self.routes(FakeResponse(data={'generatedApks': [entry]})))
        with self.assertRaises(ValueError):
            links.play_signing(session, signers=lambda apk: [':'.join(['CD'] * 32)])

    def test_falls_back_to_system_apk_variant(self):
        session = FakeSession(self.routes(FakeResponse(403, {'error': 'denied'})))
        result = links.play_signing(session, signers=lambda apk: [COLON])
        self.assertEqual(result['method'], 'systemapks.variants (fallback)')
        self.assertIn('HTTP 403', result['notes'][0])
        self.assertIn(('POST', '/systemApks/120/variants'), session.calls)

    def test_empty_track_is_an_error(self):
        with self.assertRaises(ValueError):
            links.latest_version_code({'releases': []})


def profile(entitlements):
    plist = plistlib.dumps({'TeamIdentifier': ['CL2MNNYQX3'], 'Entitlements': entitlements})
    return base64.b64encode(b'\x30\x82CMS' + plist + b'\x00signature').decode()


class FakeAsc:
    def __init__(self, capabilities):
        self.capabilities, self.posts = list(capabilities), []

    def __call__(self, request, timeout):
        url, method = request.full_url, request.get_method()
        if method == 'POST':
            body = json.loads(request.data)
            self.posts.append(body)
            self.capabilities.append(body['data']['attributes']['capabilityType'])
            data = {'data': {}}
        elif '/bundleIds/' in url and 'limit=' in url:
            # App Store Connect answers 400 PARAMETER_ERROR.ILLEGAL for limit on relationships.
            raise AssertionError(f'limit is not allowed on a relationship: {url}')
        elif '/bundleIdCapabilities' in url:
            data = {'data': [{'attributes': {'capabilityType': c}} for c in self.capabilities]}
        elif '/profiles' in url:
            data = {'data': [{'attributes': {'name': 'iOS Team Store Provisioning Profile', 'profileType': 'IOS_APP_STORE',
                                             'profileState': 'ACTIVE', 'expirationDate': '2027-01-01',
                                             'profileContent': profile({'application-identifier': 'x'})}}]}
        else:
            assert 'filter%5Bidentifier%5D=org.globulation2.glob2' in url, url
            data = {'data': [{'id': 'B1', 'attributes': {'identifier': 'org.globulation2.glob2.extra'}},
                             {'id': 'B2', 'attributes': {'identifier': 'org.globulation2.glob2', 'seedId': 'CL2MNNYQX3',
                                                         'platform': 'IOS', 'name': 'Glob2'}}]}
        return io.BytesIO(json.dumps(data).encode())


class AppStoreConnectTests(unittest.TestCase):
    def test_enables_associated_domains_once(self):
        fake = FakeAsc(['GAME_CENTER', 'IN_APP_PURCHASE'])
        result = links.ios_app_id(links.AppStoreConnect('t', opener=fake))
        self.assertEqual(result['team_id'], 'CL2MNNYQX3')
        self.assertTrue(result['enabled_associated_domains'])
        self.assertIn('ASSOCIATED_DOMAINS', result['capabilities'])
        self.assertEqual(fake.posts[0]['data']['relationships']['bundleId']['data']['id'], 'B2')
        self.assertEqual(result['profiles'][0]['team'], 'CL2MNNYQX3')
        self.assertFalse(result['profiles'][0]['associated_domains'])
        again = links.ios_app_id(links.AppStoreConnect('t', opener=fake))
        self.assertFalse(again['enabled_associated_domains'])
        self.assertEqual(len(fake.posts), 1)
        self.assertIn('already enabled', links.ios_report(again))

    def test_check_only_mode_does_not_write(self):
        fake = FakeAsc([])
        result = links.ios_app_id(links.AppStoreConnect('t', opener=fake), enable=False)
        self.assertEqual(fake.posts, [])
        self.assertIn('NOT enabled', links.ios_report(result))

    def test_http_errors_are_reported_without_the_token(self):
        def denied(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 403, 'Forbidden', {}, io.BytesIO(b'{"errors":[]}'))
        with self.assertRaises(ValueError) as caught:
            links.AppStoreConnect('secret-token', opener=denied).call('GET', '/bundleIds')
        self.assertIn('HTTP 403', str(caught.exception))
        self.assertNotIn('secret-token', str(caught.exception))

    def test_der_signature_becomes_jose_form(self):
        r, s = b'\x00' + b'\x81' * 32, b'\x05' * 31
        der = b'\x30' + bytes([4 + len(r) + len(s)]) + b'\x02' + bytes([len(r)]) + r + b'\x02' + bytes([len(s)]) + s
        self.assertEqual(links.der_to_raw_signature(der), b'\x81' * 32 + b'\x00' + b'\x05' * 31)

    @unittest.skipUnless(shutil.which('openssl'), 'needs openssl')
    def test_token_is_an_es256_jwt(self):
        with tempfile.TemporaryDirectory() as directory:
            key = Path(directory) / 'k.p8'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256',
                            '-out', str(key)], check=True, capture_output=True)
            token = links.asc_token('KEY123', 'issuer', key, now=1000)
        header, claims, signature = token.split('.')
        decode = lambda part: base64.urlsafe_b64decode(part + '=' * (-len(part) % 4))
        self.assertEqual(json.loads(decode(header)), {'alg': 'ES256', 'kid': 'KEY123', 'typ': 'JWT'})
        self.assertEqual(json.loads(decode(claims))['exp'], 1900)
        self.assertEqual(len(decode(signature)), 64)


class WorkflowTests(unittest.TestCase):
    def test_runs_only_by_owner_dispatch_in_the_release_mirror(self):
        text = WORKFLOW.read_text()
        guard = ("github.repository == 'genixpro/glob2-release' && github.repository_id == 1397722696 && "
                 "github.actor == 'genixpro' && github.actor_id == 6193625 && "
                 "github.triggering_actor == 'genixpro' && github.event_name == 'workflow_dispatch' && "
                 "github.ref == 'refs/heads/master'")
        jobs = re.split(r'^  ([a-z][a-z0-9-]*):\n', text.split('\njobs:\n', 1)[1], flags=re.M)[1:]
        self.assertEqual(jobs[0::2], ['android-play', 'android-amazon', 'ios', 'report'])
        for name, block in zip(jobs[0::2], jobs[1::2]):
            with self.subTest(job=name):
                self.assertIn(guard, block.split('steps:', 1)[0])
        self.assertEqual(re.findall(r'^  ([a-z_]+):', text.split('\non:\n', 1)[1].split('\n\n', 1)[0], re.M),
                         ['workflow_dispatch'])

    def test_never_prints_secret_material(self):
        text = WORKFLOW.read_text()
        for secret in re.findall(r'secrets\.([A-Z0-9_]+)', text):
            with self.subTest(secret=secret):
                self.assertNotRegex(text, rf'echo[^\n]*\$\{{\{{ secrets\.{secret}')
        self.assertNotIn('-storepass "', text)
        self.assertNotIn('keytool -list -v', text)


if __name__ == '__main__':
    unittest.main()
