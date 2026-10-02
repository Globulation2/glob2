"""Cross-platform parity must compare aggregate checksums and require each platform."""
from pathlib import Path
import struct
import tempfile
import unittest
from check_cli_evidence import compare


class ExecutableParityTests(unittest.TestCase):
    def fixture(self, root, platform, checksum=42, ticks=64):
        path=root/platform/'test_headless_workers_and_saved_continuation_match_complete_tick_records/one-worker/game.replay.checksums'
        path.parent.mkdir(parents=True,exist_ok=True)
        header=b'GCS1'+struct.pack('<4I',1,0,ticks,0)
        rows=b''.join(struct.pack('<5I',tick,checksum,123,0,0) for tick in range(ticks))
        path.write_bytes(header+rows)
        return path

    def test_matching_platforms_and_both_upload_layouts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.fixture(root,'cli-linux-clang')
            self.fixture(root,'cli-windows/cli-smoke')
            self.assertEqual(len(compare(root,('linux','windows'))),2)

    def test_aggregate_mismatch_is_not_hidden_by_identical_entity_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.fixture(root,'cli-linux',42)
            self.fixture(root,'cli-windows',43)
            with self.assertRaisesRegex(ValueError,'per-tick simulation differs'): compare(root)

    def test_missing_platform_and_incomplete_trace_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.fixture(root,'cli-linux-gcc'); self.fixture(root,'cli-linux-clang')
            with self.assertRaisesRegex(ValueError,'missing required platform'): compare(root,('windows',))
            self.fixture(root,'cli-linux-gcc',ticks=63)
            with self.assertRaisesRegex(ValueError,'per-tick simulation differs|64 ticks'): compare(root)

    def test_truncated_trace_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            self.fixture(root,'cli-linux')
            path=self.fixture(root,'cli-windows'); path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises((ValueError,struct.error)): compare(root)
