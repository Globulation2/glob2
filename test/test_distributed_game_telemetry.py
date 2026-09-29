#!/usr/bin/env python3
"""Lossless typed telemetry readers over the existing verified artifact channel."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.common import atomic_json, store_artifact
from tools.tournaments.game_telemetry import export, parse
from tools.tournaments.results import Results

TRACE = '''ordinary engine output
GLOB2_MEASURE team=0 tick=512 coverage_start=300 final=0 births_0=18446744073709551615
GLOB2_AI_PLAYER team=0 player=1 ai=7 generation=2 schema=1 name="AI \\"one\\"\\u000a"
GLOB2_AI_SCHEMA team=0 player=1 ai=7 generation=2 schema=1 field=future.score type=1 kind=0 unit="points" meaning="a calculation = result"
GLOB2_AI_SAMPLE team=0 player=1 ai=7 generation=2 schema=1 tick=512 future.score=-9223372036854775808 future.score@tick=509 unknown=na
GLOB2_AI_FINAL team=0 player=2 ai=8 generation=1 tick=700 count=9007199254740993
GLOB2_PERF_SESSION session=1 compiler="clang 17" unit=ns
GLOB2_PERF_FINAL session=1 tick_start=300 tick=700 scope=path.resource stride=64 calls=129 samples=2 estimated_total_ns=9007199254740993 mean_ns=1.5 stddev_ns=0.5 self_ns=na
GLOB2_MEASURE team=0 tick=700 coverage_start=300 final=1 births_0=18446744073709551615
'''

class Telemetry(unittest.TestCase):
    def test_parser_precision_escaping_and_future_fields(self):
        rows = [parse(line) for line in TRACE.splitlines()][1:]
        self.assertEqual(rows[0][1]['births_0'], 2**64-1)
        self.assertEqual(rows[1][1]['name'], 'AI "one"\n')
        self.assertEqual(rows[3][1]['future.score'], -2**63)
        self.assertEqual(rows[3][1]['future.score@tick'], 509)
        self.assertIsNone(rows[3][1]['unknown'])
        self.assertEqual(parse('GLOB2_AI_FUTURE x=1')[1]['x'], 1)
        for line in ('GLOB2_AI_SAMPLE x=', 'GLOB2_PERF_SAMPLE x=1 x=2',
                     'GLOB2_AI_PLAYER name="broken', 'GLOB2_MEASURE',
                     'GLOB2_AI_SCHEMA name="ok"junk', 'GLOB2_PERF_SAMPLE x=1e999'):
            with self.assertRaises(ValueError, msg=line): parse(line)

    def test_compressed_artifact_offline_export_and_missing_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root/'stdout.log'
            log.write_text(TRACE + 'GLOB2_AI_SAMPLE x=\n' + 'unrelated\n'*500)
            meta = store_artifact(log, root/'artifacts', compress=True)
            meta['path'] = 'stdout.log'
            self.assertEqual(meta['encoding'], 'gzip')
            record = {'id':'attempt', 'host':'host-a', 'category':'success',
                      'job':{'id':'game', 'build':'build-a', 'type':'game', 'outputs':{'telemetry':['team-timeline']}},
                      'artifacts':[meta]}
            atomic_json(root/'experiment.json', {'jobs':[record['job']]})
            atomic_json(root/'results/game.json', record)
            source = Results(root)
            rows = list(source.telemetry(record))
            self.assertEqual(len(rows), 9)
            self.assertIn('error', rows[-1])
            legacy = dict(record, id='legacy', host='host-b', artifacts=[])
            summary = export(source, [record, legacy], root)
            self.assertEqual(summary['jobs'][0]['errors'], 1)
            self.assertEqual(summary['jobs'][0]['missing_final'], [])
            self.assertEqual(summary['jobs'][1]['unavailable'], ['gameplay','ai','performance'])
            reread = [json.loads(line) for line in (root/'game-telemetry.jsonl').read_text().splitlines()]
            self.assertEqual(reread[:9], rows)
            with (root/'game-telemetry-values.csv').open() as stream:
                table = list(csv.DictReader(stream))
            value = next(r for r in table if r['field']=='future.score')
            self.assertEqual(value['value'], str(-2**63))
            self.assertEqual(value['player'], '1')
            self.assertEqual(value['generation'], '2')
            self.assertEqual(value['host'], 'host-a')
            self.assertEqual(value['value_type'], 'integer')
            # Verification remains enforced for analysis, not only transport.
            (root/'artifacts'/meta['sha256']).write_bytes(b'corrupt')
            with self.assertRaises(ValueError): list(source.telemetry(record))

if __name__ == '__main__': unittest.main()
