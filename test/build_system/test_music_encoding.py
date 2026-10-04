"""The runtime music recipe retains trimmed sample counts and publishes safely."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from encode_music import encode


@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'), 'FFmpeg tools required')
class MusicEncodingTests(unittest.TestCase):
    def test_trimmed_frames_and_failed_validation_preserves_installed_track(self):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder) / 'source.wav', Path(folder) / 'a1.opus'
            with wave.open(str(source), 'wb') as out:
                out.setparams((2, 2, 48000, 4813, 'NONE', 'PCM'))
                out.writeframes(b'\x10\x00\x20\x00' * 4813)
            record = encode(source, target, 4813)
            self.assertEqual(record['frames'], 4813)
            self.assertEqual(record['sample_rate'], 48000)
            self.assertIn('48k', record['recipe'])
            before = target.read_bytes()
            with self.assertRaises(ValueError): encode(source, target, 4814)
            self.assertEqual(target.read_bytes(), before)
            self.assertFalse(list(Path(folder).glob('.opus-*')))

    def test_circular_padding_is_trimmed_to_the_original_frame_count(self):
        import ctypes
        import ctypes.util
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder) / 'source.wav', Path(folder) / 'a1.opus'
            with wave.open(str(source), 'wb') as out:
                out.setparams((2, 2, 48000, 4813, 'NONE', 'PCM'))
                out.writeframes(b'\x10\x00\x20\x00' * 4813)
            record = encode(source, target, 4813, loop=True)
            self.assertEqual(record['frames'], 4813)
            self.assertEqual(record['loop_preroll_frames'], 4813)
            # Check the actual runtime timeline independently of FFmpeg.
            library = ctypes.util.find_library('opusfile')
            if library:
                lib = ctypes.CDLL(library)
                lib.op_open_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
                lib.op_open_file.restype = ctypes.c_void_p
                lib.op_pcm_total.argtypes = [ctypes.c_void_p, ctypes.c_int]
                lib.op_pcm_total.restype = ctypes.c_longlong
                lib.op_free.argtypes = [ctypes.c_void_p]
                error = ctypes.c_int()
                decoder = lib.op_open_file(str(target).encode(), ctypes.byref(error))
                self.assertTrue(decoder)
                try:
                    self.assertEqual(lib.op_pcm_total(decoder, -1), 4813)
                finally:
                    lib.op_free(decoder)

    def test_output_cannot_replace_source_or_use_old_extension(self):
        with self.assertRaises(ValueError): encode('a.opus', 'a.opus')
        with self.assertRaises(ValueError): encode('a.wav', 'a.ogg')


class MusicPackagingTests(unittest.TestCase):
    def test_native_install_discovers_source_sets_from_a_variant_directory(self):
        from types import SimpleNamespace
        script = Path(__file__).resolve().parents[2] / 'data/zik/SConscript'
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'source'; root.mkdir()
            names = ['intro.opus', 'menu.opus'] + [f'{name}/a{i}.opus'
                    for name in ['original', 'incoming'] for i in range(1, 4)]
            for name in names + ['retired.ogg']:
                path = root / name; path.parent.mkdir(exist_ok=True); path.touch()
            installed = []
            class Environment(dict):
                def Dir(self, path):
                    return SimpleNamespace(srcnode=lambda: SimpleNamespace(abspath=str(root)))
                def Install(self, destination, source): installed.append((destination, source))
                def Alias(self, *args): pass
            env = Environment(INSTALLDIR='/installed', TARFILE='archive')
            exec(compile(script.read_text(), str(script), 'exec'),
                 dict(env=env, Import=lambda *args: None, PackTar=lambda *args: None,
                      COMMAND_LINE_TARGETS=['install']))
            self.assertEqual(sorted(source for _, source in installed), sorted(names))
            self.assertEqual(sorted(str(Path(destination) / Path(source).name)
                                    for destination, source in installed),
                             sorted('/installed/glob2/data/zik/' + name for name in names))

    def test_vorbis_cannot_enter_runtime_assets(self):
        from package_assets import source_files
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            music = root / 'data/zik/future-set'
            music.mkdir(parents=True)
            (music/'a1.ogg').write_bytes(b'old codec')
            with self.assertRaisesRegex(ValueError, 'Convert Vorbis'):
                list(source_files(root, 'generic'))
            (music/'a1.ogg').unlink()
            (music/'a1.opus').write_bytes(b'new codec')
            self.assertEqual(list(source_files(root, 'generic')), [music/'a1.opus'])


if __name__ == '__main__': unittest.main()
