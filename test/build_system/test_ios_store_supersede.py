"""Review replacement binds explicit identities and cannot withdraw unrelated work."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
import ios_store_supersede as store


class API:
    def __init__(self):
        self.version = {'id': 'version', 'attributes': {'platform': 'IOS', 'releaseType': 'MANUAL',
                        'versionString': '0.11.200', 'appStoreState': 'WAITING_FOR_REVIEW'}}
        self.build = 'old-build'
        self.items = ['version']
        self.writes = []
        self.eligible = True
    def collect(self, path):
        if path.startswith('/v1/apps?'): return [{'id': 'app'}]
        if path.startswith('/v1/builds?'):
            return [{'id': 'new-build', 'attributes': {'processingState': 'VALID',
                    'expired': not self.eligible, 'buildAudienceType': 'APP_STORE_ELIGIBLE',
                    'usesNonExemptEncryption': False}}]
        if '/appStoreVersions?' in path: return [copy.deepcopy(self.version)]
        if '/reviewSubmissions?' in path: return [{'id': 'review', 'attributes': {'state': 'WAITING_FOR_REVIEW'}}]
        if '/items?' in path: return [{'relationships': {'appStoreVersion': {'data': {'id': value}}}} for value in self.items]
        raise AssertionError(path)
    def request(self, method, path, body=None):
        if method == 'GET':
            return {'data': {'id': self.build}} if path.endswith('/relationships/build') else {'data': copy.deepcopy(self.version)}
        self.writes.append((method, path, body))
        if path.endswith('/review'):
            self.version['attributes']['appStoreState'] = 'DEVELOPER_REJECTED'
        else:
            self.version['attributes'].update(body['data']['attributes'])
            self.build = body['data']['relationships']['build']['data']['id']
        return {}


class SupersedeTests(unittest.TestCase):
    def test_withdraws_only_selected_version_then_verifies_new_identity(self):
        api = API()
        result = store.supersede(api, {'buildNumber': '123', 'marketingVersion': '0.11.400'}, 'version', 'old-build')
        self.assertEqual(result['buildId'], 'new-build')
        self.assertEqual([call[1] for call in api.writes], ['/v1/reviewSubmissions/review', '/v1/appStoreVersions/version'])
        self.assertEqual(api.writes[0][2]['data']['attributes'], {'canceled': True})
        api.writes.clear()
        self.assertTrue(store.supersede(api, {'buildNumber': '123', 'marketingVersion': '0.11.400'}, 'version', 'old-build')['alreadyPrepared'])
        self.assertEqual(api.writes, [])

    def test_refuses_changed_identity_state_shared_submission_or_ineligible_build_before_writes(self):
        for mutation in ('build', 'state', 'release', 'items', 'eligible', 'newer'):
            api = API()
            if mutation == 'build': api.build = 'changed'
            if mutation == 'state': api.version['attributes']['appStoreState'] = 'IN_REVIEW'
            if mutation == 'release': api.version['attributes']['releaseType'] = 'AFTER_APPROVAL'
            if mutation == 'items': api.items.append('other')
            if mutation == 'eligible': api.eligible = False
            if mutation == 'newer': api.version['attributes']['versionString'] = '0.12.0'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                store.supersede(api, {'buildNumber': '123', 'marketingVersion': '0.11.400'}, 'version', 'old-build')
            self.assertEqual(api.writes, [])

    def test_resumes_withdrawn_version_without_cancelling_again(self):
        api = API(); api.version['attributes']['appStoreState'] = 'DEVELOPER_REJECTED'
        store.supersede(api, {'buildNumber': '123', 'marketingVersion': '0.11.400'}, 'version', 'old-build')
        self.assertEqual(len(api.writes), 1)
        self.assertEqual(api.writes[0][1], '/v1/appStoreVersions/version')


if __name__ == '__main__': unittest.main()
