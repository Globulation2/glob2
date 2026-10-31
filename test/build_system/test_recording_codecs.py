"""Codec profiles must exclude restricted encoders and separate build/cache inputs."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import dev_build  # Resolve the SCons module before release tests extend sys.path.
from build_layout import build_identity, default_directory
from recording_dependencies import attach, build, codec_libraries, codec_sources, native_opus_configuration, webm_build_commands
from shared_dependencies import fingerprint
from unittest.mock import patch


class RecordingCodecsTests(unittest.TestCase):
    def test_profiles_have_distinct_build_directories(self):
        identities = [build_identity({'recording_codec': codec}, host='linux')
                      for codec in ('h264', 'vp9', 'av1')]
        self.assertEqual(len({default_directory(i) for i in identities}), 3)
        self.assertNotIn('recording_codec', identities[0])

    def test_unsupported_profiles_fail_before_building(self):
        for arguments, host in [({'recording_codec': 'bad'}, 'linux'),
                                ({'recording_codec': 'vp9'}, 'darwin'),
                                ({'recording_codec': 'av1', 'target': 'web'}, 'linux'),
                                ({'recording_codec': 'vp9', 'target': 'android'}, 'linux'),
                                ({'recording_codec': 'av1', 'role': 'relay'}, 'linux')]:
            with self.subTest(arguments=arguments, host=host), self.assertRaises(ValueError):
                build_identity(arguments, host=host)

    def test_webm_sources_and_archives_exclude_h264(self):
        pins = json.loads((ROOT / 'scons/recording-versions.json').read_text())
        for codec in ('vp9', 'av1'):
            self.assertNotIn('x264', codec_libraries(codec))
            self.assertNotIn('x264', codec_sources(codec))
            self.assertNotIn('nv-codec-headers', codec_sources(codec))
            self.assertNotIn('opus', codec_libraries(codec))
            self.assertNotIn('opus', codec_sources(codec))
            for name in codec_sources(codec):
                self.assertEqual(len(pins[name]['sha256']), 64)
        self.assertNotIn('opus', pins)

    def test_webm_dependency_commands_build_encoders_without_players_or_tests(self):
        sources = {name: Path('/sources') / name for name in ('vpx', 'aom')}
        for codec in ('vp9', 'av1'):
            builds = dict(webm_build_commands(sources, Path('/prefix'), 'cc', 'c++', 'ar', 'ranlib', [], [], codec))
            self.assertNotIn('opus', builds)
            self.assertNotIn('x264', builds)
            if codec == 'vp9':
                self.assertIn('--disable-vp9-decoder', builds['vpx'])
            else:
                self.assertIn('-DCONFIG_AV1_DECODER=0', builds['aom'])

    def test_attach_supports_cold_dry_runs_and_selects_the_requested_codec(self):
        class Environment:
            def __init__(self): self.values = {}
            def Append(self, **kwargs):
                for key, values in kwargs.items(): self.values.setdefault(key, []).extend(values)
            Prepend = Append
            def File(self, path): return path
            def ParseConfig(self, command):
                self.values.setdefault('CONFIG_COMMANDS', []).append(command)
                self.Append(LIBS=['opus'])
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            for codec in ('h264', 'vp9', 'av1'):
                env = Environment()
                attach(env, directory, 'linux', codec)
                self.assertEqual(any('libx264.a' in p for p in env.values['LIBS']), codec == 'h264')
                self.assertEqual('CPPDEFINES' in env.values, codec != 'h264')
                if codec != 'h264':
                    self.assertEqual(env.values['LIBS'][-3:], ['opus', 'dl', 'm'])
                    self.assertFalse(any('libopus.a' in p for p in env.values['LIBS']))
                    self.assertEqual(env.values['CONFIG_COMMANDS'], ['pkg-config opus --cflags --libs'])
            (Path(directory) / 'recording-manifest.json').write_text(json.dumps({'identity': {'video_codec': 'h264'}}))
            with self.assertRaisesRegex(ValueError, 'does not match'):
                attach(Environment(), directory, 'linux', 'vp9')

    def test_native_opus_configuration_captures_the_actual_configure_inputs(self):
        environment = {'PATH': '/native/tools', 'PKG_CONFIG_PATH': '/native/lib/pkgconfig'}
        with patch('recording_dependencies.subprocess.check_output', side_effect=[
                '1.6.1\n', '-I/native/include/opus\n', '-L/native/lib -lopus\n',
                '-L/native/lib -lopus -lm\n']) as query:
            configuration = native_opus_configuration(environment)
        self.assertEqual(configuration, dict(version='1.6.1', cflags='-I/native/include/opus',
                                              libs='-L/native/lib -lopus', static_libs='-L/native/lib -lopus -lm'))
        self.assertEqual(len(query.call_args_list), 4)
        for call in query.call_args_list:
            self.assertEqual(call.kwargs, {'env': environment, 'text': True})
            self.assertEqual(call.args[0][0], 'pkg-config')
            self.assertEqual(call.args[0][-1], 'opus')

    def test_native_opus_api_and_link_changes_invalidate_the_shared_cache(self):
        configuration = dict(version='1.6.1', cflags='-I/native/include/opus',
                             libs='-L/native/lib -lopus', static_libs='-L/native/lib -lopus -lm')
        arguments = dict(video_codec='vp9', opus_configuration=configuration,
                         environment={'PATH': '/native/tools'})
        with patch('shared_dependencies.shutil.which', return_value=None):
            original = fingerprint(build, arguments)
            for key in configuration:
                changed = dict(configuration)
                changed[key] += '-changed'
                self.assertNotEqual(original, fingerprint(build, dict(arguments, opus_configuration=changed)))
