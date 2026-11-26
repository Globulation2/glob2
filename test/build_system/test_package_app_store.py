"""Mac App Store signing binds the sandbox claims to the profile and requires Declared Age Range."""
import plistlib
import sys
from pathlib import Path
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'platforms/macos'))
import package_app_store


def profile(**claims):
    return plistlib.dumps({'TeamIdentifier': ['TEAM123456'], 'Entitlements': {
        'com.apple.application-identifier': 'TEAM123456.org.globulation2.glob2', **claims}})


class DistributionEntitlementsTests(unittest.TestCase):
    def entitlements(self, decoded):
        with mock.patch.object(package_app_store.subprocess, 'check_output', return_value=decoded):
            return package_app_store.distribution_entitlements(Path('glob2.provisionprofile'), 'org.globulation2.glob2')

    def test_adds_declared_age_range_when_the_profile_grants_it(self):
        signed = self.entitlements(profile(**{package_app_store.AGE_RANGE_ENTITLEMENT: True}))
        self.assertIs(signed[package_app_store.AGE_RANGE_ENTITLEMENT], True)
        self.assertEqual(signed['com.apple.developer.team-identifier'], 'TEAM123456')
        self.assertIs(signed['com.apple.security.app-sandbox'], True)

    def test_refuses_a_profile_without_declared_age_range(self):
        with self.assertRaisesRegex(ValueError, 'Declared Age Range'):
            self.entitlements(profile())

    def test_ad_hoc_entitlements_leave_the_restricted_claim_out(self):
        with package_app_store.ENTITLEMENTS.open('rb') as file:
            self.assertNotIn(package_app_store.AGE_RANGE_ENTITLEMENT, plistlib.load(file))


if __name__ == '__main__':
    unittest.main()
