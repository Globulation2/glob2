"""A failed tool download must never be installed or weaken the expected hash."""
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
import tool_archives

class ToolArchiveTests(unittest.TestCase):
    def test_partial_response_is_retried_and_only_verified_bytes_are_published(self):
        expected = b'complete pinned archive'
        artifact = {'url': 'https://example.test/tool.zip', 'sha256': hashlib.sha256(expected).hexdigest()}
        with tempfile.TemporaryDirectory() as directory, patch.object(tool_archives.urllib.request, 'urlopen', side_effect=[io.BytesIO(b'partial'), io.BytesIO(expected)]) as opener:
            target = tool_archives.download(artifact, directory)
            self.assertEqual(target.read_bytes(), expected)
            self.assertEqual(opener.call_count, 2)
            self.assertEqual(list(Path(directory).iterdir()), [target])

    def test_bad_responses_fail_after_three_attempts_without_an_archive(self):
        artifact = {'url': 'https://example.test/tool.zip', 'sha256': hashlib.sha256(b'good').hexdigest()}
        with tempfile.TemporaryDirectory() as directory, patch.object(tool_archives.urllib.request, 'urlopen', side_effect=[io.BytesIO(b'bad') for _ in range(3)]) as opener:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                tool_archives.download(artifact, directory)
            self.assertEqual(opener.call_count, 3)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_verified_cache_is_used_without_a_network_request(self):
        expected = b'cached pinned archive'
        artifact = {'url': 'https://example.test/tool.zip', 'sha256': hashlib.sha256(expected).hexdigest()}
        with tempfile.TemporaryDirectory() as directory, patch.object(tool_archives.urllib.request, 'urlopen') as opener:
            target = Path(directory) / 'tool.zip'
            target.write_bytes(expected)
            self.assertEqual(tool_archives.download(artifact, directory), target)
            opener.assert_not_called()
