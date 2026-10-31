"""Distro variants never reuse enabled objects or provision private codecs."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
# Resolve this before release-archive tests prepend tools/ (which also has a
# dev_build.py CLI). build_identity imports it lazily during each test.
import dev_build
from build_layout import build_identity, default_directory, prepare_directory
from recording_config import attach_system, sources, CODEC_SOURCES
from sources import GAG_SOURCES


class RecordingConfigTests(unittest.TestCase):
    def test_default_identity_and_three_modes_are_isolated(self):
        bundled = build_identity({}, 'linux')
        self.assertEqual(bundled, build_identity({'recording': 'bundled'}, 'linux'))
        identities = [build_identity({'recording': mode}, 'linux')
                      for mode in ('bundled', 'system', 'disabled')]
        self.assertEqual(len({default_directory(x) for x in identities}), 3)
        with tempfile.TemporaryDirectory() as directory:
            prepare_directory(directory, bundled)
            for other in identities[1:]:
                with self.assertRaises(ValueError):
                    prepare_directory(directory, other)

    def test_invalid_or_unsupported_modes_fail_before_building(self):
        for args, host in [({'recording': 'auto'}, 'linux'),
                           ({'recording': 'disabled', 'target': 'web'}, 'linux'),
                           ({'recording': 'system', 'target': 'android'}, 'linux'),
                           ({'recording': 'disabled', 'role': 'relay'}, 'linux'),
                           ({'recording': 'system', 'mingw': '1'}, 'linux'),
                           ({'recording': 'disabled'}, 'darwin')]:
            with self.subTest(args=args, host=host), self.assertRaises(ValueError):
                build_identity(args, host)

    def test_disabled_source_selection_omits_all_codec_implementations(self):
        selected = sources(GAG_SOURCES, 'disabled')
        self.assertTrue(CODEC_SOURCES.isdisjoint(selected))
        self.assertIn('RecordingDisabled.cpp', selected)
        self.assertIn('RecordingMetadata.cpp', selected)
        self.assertEqual(sources(GAG_SOURCES, 'system'), list(GAG_SOURCES))
        self.assertEqual(sources(GAG_SOURCES, 'bundled'), list(GAG_SOURCES))

    def test_system_libraries_are_resolved_by_pkg_config_only(self):
        env = Mock()
        attach_system(env)
        env.ParseConfig.assert_called_once_with(
            'pkg-config --cflags --libs libavformat libavcodec libswscale libswresample libavutil')

    def test_installed_paths_are_a_native_linux_client_path_choice(self):
        build_identity({'installed_paths': '1'}, 'linux')
        for arguments, host in [({'installed_paths': '1'}, 'darwin'),
                                ({'installed_paths': '1', 'role': 'relay'}, 'linux'),
                                ({'installed_paths': '1', 'target': 'web'}, 'linux'),
                                ({'installed_paths': '1', 'mingw': '1'}, 'linux')]:
            with self.subTest(arguments=arguments, host=host), self.assertRaises(ValueError):
                build_identity(arguments, host)
