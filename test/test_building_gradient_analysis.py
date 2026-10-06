"""Guard measurement interpretation: nested timing, repeated fields and overlap."""
import csv
import tempfile
import unittest
from pathlib import Path
from analyze_building_gradients import analyze


class BuildingGradientAnalysisTest(unittest.TestCase):
    def run_analysis(self, events):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory/'building-gradients-ticks.csv').write_text(
                'tick,tick_ns,tick_cpu_ns,generation_begin,generation_end,buildings,flags,units,fields,unfinished,dropped_events\n'
                '10,1000,600,1,1,2,0,10,2,2,0\n'
                '11,1000,600,1,1,2,0,10,2,0,0\n')
            with (directory/'building-gradients-events.csv').open('w') as stream:
                writer = csv.writer(stream)
                writer.writerow(('tick','gid','swim','phase','kind','reason','generation',
                                 'snapshot_generation','start_ns','duration_ns','cpu_ns','popped','completed'))
                for gid, kind, start, duration in events:
                    writer.writerow((10,gid,0,'units',kind,'query',1,1,start,duration,duration//2,5,0))
            (directory/'result.json').write_text('{"compute_experiments":"ai"}')
            return analyze(directory)

    def test_repeated_extensions_are_one_field_and_nested_round_trip_is_excluded(self):
        result = self.run_analysis([(1,'resume',0,100), (1,'resume',100,100),
                                    (2,'finish',300,200), (2,'round_trip',250,300)])
        self.assertEqual(result['repeated_field_extensions'], 1)
        self.assertEqual(result['ticks_with_multiple_fields'], 1)
        self.assertEqual(result['building_time_share'], .2)
        self.assertAlmostEqual(result['building_cpu_share_of_simulation_thread'], 1/6)
        self.assertEqual(result['optimistic_independent_field_bounds']['2']['building_work_lower_bound_ms'], .0002)

    def test_parallel_durations_are_not_serial_wall_time(self):
        result = self.run_analysis([(1,'resume',0,300), (2,'resume',0,300)])
        self.assertEqual(result['building_time_share'], .15)
        self.assertEqual(result['ticks_with_overlapping_building_work'], 1)
        self.assertIsNone(result['optimistic_independent_field_bounds']['2']['whole_tick_speedup_ceiling'])

    def test_idle_ticks_remain_in_the_population(self):
        result = self.run_analysis([])
        self.assertEqual(result['ticks'], 2)
        self.assertEqual(result['ticks_with_work'], 0)
        self.assertEqual(result['building_time_share'], 0)
        self.assertEqual(result['optimistic_independent_field_bounds']['8']['whole_tick_speedup_ceiling'], 1)


if __name__ == '__main__':
    unittest.main()
