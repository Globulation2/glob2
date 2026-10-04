"""Windows codec builds select the MSYS shell rather than the WSL launcher."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
from recording_dependencies import configure_bash


class RecordingShellTests(unittest.TestCase):
    def test_windows_uses_bash_beside_the_posix_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            shell = Path(directory) / 'sh.exe'
            bash = Path(directory) / 'bash.exe'
            shell.touch()
            bash.touch()
            with patch('recording_dependencies.platform.system', return_value='Windows'), patch('recording_dependencies.shutil.which', return_value=str(shell)) as locate:
                self.assertEqual(configure_bash({'PATH': 'toolchain-path'}), str(bash))
                locate.assert_called_once_with('sh', path='toolchain-path')

    def test_windows_missing_posix_bash_reports_the_required_toolchain(self):
        with patch('recording_dependencies.platform.system', return_value='Windows'), patch('recording_dependencies.shutil.which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'MSYS Bash'):
                configure_bash({})

    def test_native_platforms_retain_bash_lookup(self):
        with patch('recording_dependencies.platform.system', return_value='Linux'):
            self.assertEqual(configure_bash({}), 'bash')
