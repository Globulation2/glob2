"""Cross-platform evidence compares only the platforms whose producer succeeded."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import ci_compare_evidence as evidence  # noqa: E402


class PresentPlatformsTest(unittest.TestCase):
    def test_failed_producers_are_left_out_with_a_notice(self):
        upstream = {'linux': {'result': 'success'}, 'windows': {'result': 'failure'},
                    'web-test': {'result': 'cancelled'}}
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            kept = evidence.present(upstream, ['ubuntu-24.04', 'windows', 'browsers'])
        self.assertEqual(kept, ['ubuntu-24.04'])
        self.assertIn('::notice::windows evidence is not compared: upstream job windows failure', out.getvalue())
        self.assertIn('upstream job web-test cancelled', out.getvalue())

    def test_absent_needs_context_keeps_every_platform(self):
        self.assertEqual(evidence.present({}, ['ubuntu-24.04', 'windows']), ['ubuntu-24.04', 'windows'])


class VerifyTraceTest(unittest.TestCase):
    def write(self, root, name, text):
        path = root / name / 'verify-match.checksums.txt'
        path.parent.mkdir(parents=True)
        path.write_text(text)

    def test_present_traces_must_agree(self):
        trace = 'tick\n' * 700
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write(root, 'a', trace)
            self.write(root, 'b', trace)
            self.assertEqual(len(evidence.compare_verify_traces(root)[0]), 2)
            with self.assertRaisesRegex(ValueError, 'Expected 3'):
                evidence.compare_verify_traces(root, 3)
            self.write(root, 'c', trace + 'extra\n')
            with self.assertRaisesRegex(ValueError, 'Verification differs'):
                evidence.compare_verify_traces(root)

    def test_fewer_than_two_traces_compare_nothing(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            self.write(Path(tmp), 'a', 'tick\n' * 700)
            self.assertIsNone(evidence.compare_verify_traces(Path(tmp))[1])


if __name__ == '__main__':
    unittest.main()
