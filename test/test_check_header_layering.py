#!/usr/bin/env python3
"""Contracts for test/check_header_layering.py."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_header_layering as layering  # noqa: E402


class VersionDefaultTest(unittest.TestCase):
    def matches(self, code):
        return bool(layering.VERSION_DEFAULT.search(code))

    def test_default_arguments_and_members_are_caught(self):
        self.assertTrue(self.matches('void load(Game &, InputStream *, int versionMinor = VERSION_MINOR);'))
        self.assertTrue(self.matches('SimDataFile simRevisionEntry(int revision = SIM_REVISION);'))
        self.assertTrue(self.matches('int loadingGradientVersion=FILE_FORMAT_VERSION_ROUTING_RATIO_POLICY;'))
        self.assertTrue(self.matches('Reader(Stream* s, bool compact=false, int version=VERSION_MINOR) {}'))

    def test_comparisons_are_not_defaults(self):
        self.assertFalse(self.matches('if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG) return;'))
        self.assertFalse(self.matches('return version == FILE_FORMAT_VERSION_SNAPSHOT_CHECKSUM || x;'))
        self.assertFalse(self.matches('CHECK(v <= VERSION_MINOR);'))


class LayersTest(unittest.TestCase):
    layers = layering.Layers({
        'ranks': {'src/map': 3, 'src/*/types': 2, 'src/map/editor': 7, 'src/game': 5, 'src/render': 7},
        'simulation': ['src/map', 'src/game'],
        'presentation': ['src/render', 'src/map/editor'],
    })

    def test_longest_prefix_and_wildcard(self):
        self.assertEqual(self.layers.rank('src/map/Map.h'), 3)
        self.assertEqual(self.layers.rank('src/map/editor/Brush.h'), 7)
        self.assertEqual(self.layers.rank('src/unit/types/UnitType.h'), 2)
        self.assertIsNone(self.layers.rank('src/unknown/X.h'))

    def test_presentation_inside_simulation_is_not_simulation(self):
        self.assertTrue(self.layers.is_simulation('src/map/Map.h'))
        self.assertFalse(self.layers.is_simulation('src/map/editor/Brush.h'))


class CycleTest(unittest.TestCase):
    def test_tarjan_groups_cycles_only(self):
        component = layering.strongly_connected({'a': {'b'}, 'b': {'a', 'c'}, 'c': set()})
        self.assertEqual(component['a'], component['b'])
        self.assertNotEqual(component['a'], component['c'])

    def test_directory_depth(self):
        self.assertEqual(layering.directory('src/map/gradient/X.h', 1), 'src/map')
        self.assertEqual(layering.directory('src/map/gradient/X.h', 2), 'src/map/gradient')
        self.assertIsNone(layering.directory('src/map/X.h', 2))


class BudgetSlackTest(unittest.TestCase):
    def test_a_new_source_file_does_not_trip_budgets_but_a_new_wide_include_does(self):
        import json, tempfile
        from unittest.mock import patch
        budget = json.loads(layering.BUDGET.read_text())
        header = 'src/game/Game.h'
        real = layering.IncludeGraph.fanin
        def shifted(extra):
            return lambda self, units=None: {**real(self, units), header: budget[header] + extra}
        def warned():
            layering.WARNINGS.clear()
            with patch('builtins.print'):
                errors = layering.check()
            self.assertEqual([e for e in errors if header in e], [], 'fan-in budgets never fail')
            return [w for w in layering.WARNINGS if header in w]
        with patch.object(layering.IncludeGraph, 'fanin', shifted(1)):
            self.assertEqual(warned(), [])
        with patch.object(layering.IncludeGraph, 'fanin', shifted(100)):
            self.assertTrue(warned())


class StaleAllowlistTest(unittest.TestCase):
    def test_resolved_edges_warn_and_new_edges_fail(self):
        from unittest.mock import patch
        real = layering.read_lines
        stale = 'src/nowhere/Gone.h -> src/render/Gone.h'
        def with_stale(path):
            lines = real(path)
            return lines + [stale] if path == layering.LAYER_ALLOWLIST else lines
        layering.WARNINGS.clear()
        with patch.object(layering, 'read_lines', with_stale), patch('builtins.print'):
            self.assertEqual(layering.check(), [])
        self.assertTrue([w for w in layering.WARNINGS if 'allowlist entries no longer occur' in w])
        real_violations = layering.layer_violations
        with patch.object(layering, 'layer_violations', lambda g, l: real_violations(g, l) + [stale]):
            self.assertTrue([e for e in layering.check() if 'new layering edge' in e])


class GlobalContainerRatchetTest(unittest.TestCase):
    def test_new_simulation_uses_fail(self):
        from unittest.mock import patch
        real = layering.global_container_uses
        with patch.object(layering, 'global_container_uses', lambda g, l: {**real(g, l), 'src/unit': 1}):
            self.assertTrue([e for e in layering.check() if 'src/unit uses globalContainer' in e])


class SystemHeaderFaninTest(unittest.TestCase):
    def test_counts_units_reaching_a_system_header_through_any_project_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {'a.cpp': '#include "wide.h"\n', 'b.cpp': '#include <chrono>\n', 'c.cpp': '',
                     'wide.h': '  #  include <chrono>\n#include <vector>\n'}
            for name, text in files.items():
                (root / name).write_text(text)
            reach = {'a.cpp': {'wide.h'}, 'b.cpp': set(), 'c.cpp': set()}

            class Graph:
                pass
            graph = Graph()
            graph.root, graph.client_units, graph.reach = root, list(reach), reach.__getitem__
            self.assertEqual(layering.system_header_fanin(graph, ['<chrono>', '<valarray>']),
                             {'<chrono>': 2, '<valarray>': 0})


class RepositoryTest(unittest.TestCase):
    def test_repository_passes(self):
        self.assertEqual(layering.check(), [])


if __name__ == '__main__':
    unittest.main()
