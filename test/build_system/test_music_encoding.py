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

    def test_output_cannot_replace_source_or_use_old_extension(self):
        with self.assertRaises(ValueError): encode('a.opus', 'a.opus')
        with self.assertRaises(ValueError): encode('a.wav', 'a.ogg')


class MusicPackagingTests(unittest.TestCase):
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
