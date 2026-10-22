"""The protected store inspector exposes identities without store writes or private metadata."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'mobile'))
import ios_store_inspect


class StoreInspectionTests(unittest.TestCase):
    def test_only_reads_and_limits_output_to_version_identity(self):
        api = Mock()
        api.collect.side_effect = [
            [{'id': 'app', 'attributes': {'private': 'not returned'}}],
            [{'id': 'draft', 'attributes': {'versionString': '0.11.302',
              'appStoreState': 'PREPARE_FOR_SUBMISSION', 'releaseType': 'MANUAL',
              'contactEmail': 'not returned'}}],
        ]
        api.request.return_value = {'data': {'id': 'old-build'}}
        result = ios_store_inspect.inspect(api, {'marketingVersion': '0.11.400',
                                                'buildNumber': '123'})
        self.assertEqual(result, {'appId': 'app', 'selectedMarketingVersion': '0.11.400',
                                 'selectedBuildNumber': '123', 'versions': [
                                     {'versionId': 'draft', 'version': '0.11.302',
                                      'state': 'PREPARE_FOR_SUBMISSION',
                                      'releaseType': 'MANUAL', 'buildId': 'old-build'}]})
        api.request.assert_called_once_with('GET', '/v1/appStoreVersions/draft/relationships/build')


if __name__ == '__main__':
    unittest.main()
