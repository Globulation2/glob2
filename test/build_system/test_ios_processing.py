import base64
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
import ios_processing as processing


def build(**changes):
    return {'id': 'exact-build', 'attributes': {'version': '123', 'processingState': 'VALID',
        'buildAudienceType': 'APP_STORE_ELIGIBLE', 'usesNonExemptEncryption': False,
        'expired': False, **changes}}


class ProcessingTests(unittest.TestCase):
    def observe(self, pages, **options):
        calls = []
        now = [0]
        def api():
            calls.append('new-token')
            class API:
                def collect(self, path):
                    calls.append(path)
                    if path.startswith('/v1/apps?'):
                        return [{'id': 'glob2-app'}]
                    return copy.deepcopy(pages.pop(0)) if len(pages) > 1 else copy.deepcopy(pages[0])
            return API()
        def sleep(seconds): now[0] += seconds
        result = processing.wait_for_build(api, '123', '0.11.5', options.pop('audience', 'external'),
            timeout=60, interval=30, clock=lambda: now[0], sleep=sleep, **options)
        return result, calls

    def test_waits_for_visibility_and_processing_with_fresh_tokens_and_exact_filters(self):
        result, calls = self.observe([[], [build(processingState='PROCESSING')], [build()]])
        self.assertTrue(result['appStoreEligible'])
        self.assertEqual(result['buildId'], 'exact-build')
        self.assertEqual(calls.count('new-token'), 4)
        self.assertIn('filter%5Bversion%5D=123', calls[-1])
        self.assertIn('filter%5BpreReleaseVersion.version%5D=0.11.5', calls[-1])
        self.assertIn('filter%5BpreReleaseVersion.platform%5D=IOS', calls[-1])

    def test_rejects_failed_unknown_expired_internal_and_unresolved_builds(self):
        for changes in ({'processingState': 'FAILED'}, {'processingState': 'INVALID'},
                        {'processingState': None}, {'expired': True}, {'expired': None},
                        {'buildAudienceType': 'INTERNAL_ONLY'}, {'version': '124'},
                        {'usesNonExemptEncryption': None}, {'usesNonExemptEncryption': True}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.observe([[build(**changes)]])

    def test_rejects_duplicate_builds(self):
        with self.assertRaisesRegex(ValueError, 'multiple builds'):
            self.observe([[build(), build()]])

    def test_invisible_or_pending_upload_times_out_without_success(self):
        for page in ([], [build(processingState='PROCESSING')]):
            with self.subTest(page=page), self.assertRaisesRegex(ValueError, 'delivery errors'):
                self.observe([page])

    def test_internal_retry_preserves_internal_audience_without_claiming_store_eligibility(self):
        result, _ = self.observe([[build(buildAudienceType='INTERNAL_ONLY')]], audience='internal')
        self.assertFalse(result['appStoreEligible'])

    @unittest.skipUnless(shutil.which('openssl'), 'OpenSSL is required for the signing contract')
    def test_authentication_signature_verifies_and_has_short_lifetime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key = root / 'key.pem'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt',
                'ec_paramgen_curve:P-256', '-out', str(key)], check=True, capture_output=True)
            token = processing.jwt_token(key, 'public-key-id', 'public-issuer')
            header, payload, raw = token.split('.')
            def decode(value): return base64.urlsafe_b64decode(value + '=' * (-len(value) % 4))
            claims = json.loads(decode(payload))
            self.assertEqual(claims['exp'] - claims['iat'], 600)
            self.assertEqual(claims['aud'], 'appstoreconnect-v1')
            self.assertEqual(json.loads(decode(header))['alg'], 'ES256')
            signature = decode(raw)
            self.assertEqual(len(signature), 64)
            def integer(value):
                value = value.lstrip(b'\0') or b'\0'
                if value[0] & 128: value = b'\0' + value
                return bytes([2, len(value)]) + value
            values = integer(signature[:32]) + integer(signature[32:])
            (root / 'signature.der').write_bytes(bytes([48, len(values)]) + values)
            (root / 'payload').write_bytes((header + '.' + payload).encode())
            subprocess.run(['openssl', 'pkey', '-in', str(key), '-pubout', '-out',
                str(root / 'public.pem')], check=True, capture_output=True)
            subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(root / 'public.pem'),
                '-signature', str(root / 'signature.der'), str(root / 'payload')], check=True, capture_output=True)


if __name__ == '__main__': unittest.main()
