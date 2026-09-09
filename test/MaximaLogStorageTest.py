#!/usr/bin/env python3
import sys, unittest, tempfile, gzip
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import run_maxima_switch_ablation as a
class LogStorageTest(unittest.TestCase):
    def test_lossless_atomic_compression(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"branch.log"
            content="telemetry café\n"*10000
            result=a.write_run_log(p,content)
            self.assertEqual(a.read_run_log(p),content)
            self.assertLess(result.stat().st_size,len(content.encode())/10)
            self.assertFalse(p.exists())
            self.assertFalse(list(Path(tmp).glob("*.pending")))
    def test_legacy_and_corrupt_logs(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"branch.log";p.write_text("legacy")
            self.assertEqual(a.read_run_log(p),"legacy")
            p.with_suffix(".log.gz").write_bytes(b"corrupt")
            self.assertIsNone(a.read_run_log(p))
    def test_completed_compressed_run_is_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"case-on-0.log"
            content="MAXIMA_CHECKPOINT_START\ttick=0\nMAXIMA_CHECKPOINT_RESULT\ttick=10\n"
            a.write_run_log(p,content)
            with patch.object(a,"parse_run_output",return_value={"error":""}) as parse:
                result=a.cached_run({"checkpoint_id":"case"},True,0,10,Path(tmp),3)
                self.assertEqual(result["id"],3)
                self.assertEqual(parse.call_args.args[4],content)
    def test_incomplete_run_is_not_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            a.write_run_log(Path(tmp)/"case-on-0.log","MAXIMA_CHECKPOINT_START\ttick=0\n")
            self.assertIsNone(a.cached_run({"checkpoint_id":"case"},True,0,10,Path(tmp),3))
if __name__=="__main__":unittest.main()
