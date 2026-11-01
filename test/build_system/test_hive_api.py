"""The player API header is generated from one maintained contract."""
import importlib.util
from pathlib import Path
import unittest
import os
import sys
import tempfile
import types
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hive_api', ROOT / 'scons/hive_api.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)


class HiveAPITests(unittest.TestCase):
    def test_header_retains_the_exact_player_contract(self):
        contract = (ROOT / 'platform/packages/hive/src/commander-api.txt').read_text()
        header = api.render(contract)
        player = api.player_contract(contract)
        self.assertIn('R"GLOB2_API(' + player + ')GLOB2_API"', header)
        self.assertIn('A step returns', player)
        self.assertIn('ctx.game.units', player)
        self.assertNotIn('new UUID', player)
        self.assertNotIn('paid follow-up', player)
        self.assertIn('new UUID', contract)  # Online clients retain their control contract.

    def test_missing_section_markers_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'separate online management'):
            api.render('unstructured contract')

    def test_bad_delimiter_is_rejected(self):
        with self.assertRaises(ValueError):
            api.render(')GLOB2_API"\n## Online program management\nOnline\n## Player API\nPlayer')

    def test_native_variant_cwd_generates_in_the_project_build_include_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nested = root / 'build/client/src'
            nested.mkdir(parents=True)
            contract = root / 'platform/packages/hive/src/commander-api.txt'
            contract.parent.mkdir(parents=True)
            contract.write_text('Shared\n## Online program management\nOnline\n## Player API\nPlayer\n')
            script = types.ModuleType('SCons.Script')
            script.Action = lambda function, _: function
            package = types.ModuleType('SCons')
            package.Script = script
            layout = types.ModuleType('build_layout')

            def write(path, content):
                path = Path(path)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)

            layout.write_if_changed = write

            class Environment:
                def Dir(self, path):
                    assert path == '#'
                    return types.SimpleNamespace(abspath=str(root))

                def File(self, path):
                    return root / path.removeprefix('#')

                def Command(self, target, sources, action):
                    action([Path(target)], sources, self)
                    return [Path(target)]

            before = Path.cwd()
            try:
                os.chdir(nested)
                with patch.dict(sys.modules, {'SCons': package, 'SCons.Script': script, 'build_layout': layout}):
                    generated = api.configure(Environment(), 'build/client')
            finally:
                os.chdir(before)
            expected = root / 'build/client/include/hive/PlayerAPIContract.h'
            self.assertEqual(generated, [expected])
            self.assertEqual(expected.read_text(), api.render(contract.read_text()))


if __name__ == '__main__':
    unittest.main()
