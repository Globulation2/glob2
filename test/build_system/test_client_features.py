"""Release capabilities are resolved independently and cannot share stale objects."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
from client_features import resolve, header
from build_layout import build_identity, default_directory
from dev_build import dependency_identity

class ClientFeaturesTests(unittest.TestCase):
    def test_release_is_free_and_development_is_full(self):
        self.assertTrue(resolve({})['client_features']['commander'])
        free = resolve({'release': '1'})
        self.assertFalse(free['client_features']['commander'])
        self.assertFalse(free['client_features']['authoring_links'])
        self.assertTrue(free['client_features']['community_ai'])
        self.assertTrue(free['client_features']['community_generators'])
        self.assertIn('#define GLOB2_FEATURE_COMMANDER 0', header(free))

    def test_store_capabilities_are_independent(self):
        apple = resolve({'target': 'ios', 'release': 1})
        self.assertFalse(apple['client_features']['community_ai'])
        self.assertFalse(apple['client_features']['community_generators'])
        android = resolve({'target': 'android', 'release': 1})
        self.assertTrue(android['client_features']['community_ai'])
        self.assertTrue(resolve({'release': 1, 'distribution': 'app_store', 'feature_community_ai': 1})['client_features']['community_ai'])
        override = resolve({'client_profile': 'full', 'feature_commander': 'off'})
        self.assertFalse(override['client_features']['commander'])
        self.assertTrue(override['client_features']['authoring_links'])

    def test_invalid_values_fail_closed(self):
        for args in ({'client_profile': 'unknown'}, {'distribution': 'unknown'}, {'feature_commander': 'auto'}):
            with self.subTest(args=args), self.assertRaises(ValueError): resolve(args)

    def test_feature_variants_isolate_outputs_but_reuse_dependencies(self):
        full = build_identity({}, 'darwin')
        free = build_identity({'client_profile': 'free'}, 'darwin')
        apple = build_identity({'release': 1, 'distribution': 'app_store'}, 'darwin')
        direct = build_identity({'release': 1}, 'darwin')
        self.assertEqual(str(default_directory(direct)), 'build/darwin/client/release')
        self.assertNotEqual(default_directory(full), default_directory(free))
        self.assertNotEqual(default_directory(apple), default_directory(direct))
        self.assertEqual(dependency_identity(full), dependency_identity(free))
        self.assertEqual(dependency_identity(apple), dependency_identity(direct))

if __name__ == '__main__': unittest.main()
