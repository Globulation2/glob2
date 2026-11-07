#!/usr/bin/env python3
"""tools/link_graph.py: edges, closures, components and module restriction."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import link_graph  # noqa: E402


def table(defines=(), weak=(), uses=()):
    return (set(defines), set(weak), set(uses))


NODES = {
    'map/Map.o': table(['_ZN3Map4stepEv'], ['_ZN6inlineEv'], ['_ZN4Game7observeEv', 'globalContainer', 'memcpy']),
    'game/Game.o': table(['_ZN4Game7observeEv'], [], ['_ZN3Map4stepEv', '_ZN2UI4drawEv']),
    'hud/UI.o': table(['_ZN2UI4drawEv'], ['_ZN6inlineEv'], []),
    'app/Glob2.o': table(['main', 'globalContainer'], [], ['_ZN4Game7observeEv']),
    'libgag.a(Sprite.o)': table(['_ZN6Sprite4drawEv'], [], []),
    'test/stub.o': table(['_ZN2UI4drawEv'], [], []),
}


class LinkGraphTest(unittest.TestCase):
    def setUp(self):
        self.graph = link_graph.Graph(NODES)

    def test_edges_follow_strong_definitions_and_skip_entry_points(self):
        self.assertEqual(set(self.graph.edges['map/Map.o']), {'game/Game.o'})
        self.assertEqual(set(self.graph.edges['game/Game.o']), {'map/Map.o', 'hud/UI.o'})
        self.assertIn('globalContainer', self.graph.unresolved['map/Map.o'])
        self.assertIn('globalContainer', self.graph.program_globals)
        self.assertEqual(self.graph.entry_points, {'app/Glob2.o'})
        # A weak-only symbol adds no edge unless asked.
        self.assertNotIn('hud/UI.o', self.graph.edges['map/Map.o'])

    def test_closure_component_and_path(self):
        self.assertEqual(self.graph.closure(['map/Map.o']), {'map/Map.o', 'game/Game.o', 'hud/UI.o'})
        self.assertEqual(self.graph.sccs()[0], ['game/Game.o', 'map/Map.o'])
        self.assertEqual(self.graph.path('map/Map.o', 'hud/UI.o'), ['map/Map.o', 'game/Game.o', 'hud/UI.o'])
        self.assertIsNone(self.graph.path('hud/UI.o', 'map/Map.o'))
        self.assertEqual(self.graph.resolve('Map.o'), 'map/Map.o')
        with self.assertRaises(SystemExit):
            self.graph.resolve('.o')

    def test_restriction_reports_what_lies_outside(self):
        inner = self.graph.restrict({'map/Map.o', 'libgag.a(Sprite.o)'})
        self.assertEqual(inner.closure(['map/Map.o']), {'map/Map.o'})
        self.assertEqual(inner.outside['map/Map.o'], {'game/Game.o': ['_ZN4Game7observeEv']})
        self.assertEqual(inner.sccs(), [['libgag.a(Sprite.o)'], ['map/Map.o']])

    def test_groups_come_from_the_source_manifest(self):
        objects = link_graph.group_objects('map+sim')
        self.assertIn('map/Map.o', objects)
        self.assertIn('game/Game.o', objects)
        self.assertNotIn('hud/GameGUI.o', objects)
        self.assertTrue(link_graph.is_library('libgag.a(Sprite.o)'))
        with self.assertRaises(SystemExit):
            link_graph.group_objects('render')


if __name__ == '__main__':
    unittest.main()
