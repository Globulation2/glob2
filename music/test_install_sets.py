"""Verify encoding failures cannot replace an installed soundtrack trio."""
from array import array
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave

import install_sets


class InstallSetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.encoder = install_sets.load_encoder()
        except (RuntimeError, OSError) as error:
            raise unittest.SkipTest(str(error))

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sources = [self.root / f'{index}.wav' for index in range(3)]
        samples = array('h', [((index % 101) - 50) * 100 for index in range(8192)])
        for path in self.sources:
            with wave.open(str(path), 'wb') as output:
                output.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
                output.writeframes(samples.tobytes())
        self.destination = self.root / 'installed'
        self.destination.mkdir()
        for index in range(1, 4):
            (self.destination / f'a{index}.ogg').write_bytes(b'existing soundtrack')

    def assert_originals_preserved(self):
        self.assertEqual(len(list(self.destination.iterdir())), 3)
        for path in self.destination.iterdir():
            self.assertEqual(path.read_bytes(), b'existing soundtrack')

    def test_encode_and_verify_complete_trio(self):
        records = install_sets.install_trio(self.encoder, self.sources, self.destination)
        self.assertEqual([record['frames'] for record in records], [4096] * 3)
        for record in records:
            self.assertGreater(record['decoded_peak'], 0)
        for path in self.destination.iterdir():
            self.assertEqual(path.read_bytes()[:4], b'OggS')

    def test_invalid_source_preserves_installed_trio(self):
        self.sources[2].write_bytes(b'invalid WAV')
        with self.assertRaises(wave.Error):
            install_sets.install_trio(self.encoder, self.sources, self.destination)
        self.assert_originals_preserved()

    def test_encoder_failure_preserves_installed_trio(self):
        real_encode = install_sets.encode_track
        calls = 0

        def fail_second(*args):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise RuntimeError('Simulated encoder failure')
            return real_encode(*args)

        with patch.object(install_sets, 'encode_track', side_effect=fail_second):
            with self.assertRaisesRegex(RuntimeError, 'Simulated'):
                install_sets.install_trio(self.encoder, self.sources, self.destination)
        self.assert_originals_preserved()


if __name__ == '__main__':
    unittest.main()
