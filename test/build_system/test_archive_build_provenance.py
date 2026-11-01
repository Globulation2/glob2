"""Source archives can register unused test targets without borrowing parent Git identity."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]


class ArchiveBuildProvenanceTests(unittest.TestCase):
    def test_unused_test_targets_do_not_probe_git_and_requested_evidence_fails_clearly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # No test/build_provenance.py or .git: even loading that module would
            # fail here. A game build should register the target successfully.
            env = Mock()
            env.Dir.return_value.srcnode.return_value.abspath = str(root)
            script = types.ModuleType('SCons.Script')
            script.Action = lambda function, label: function
            script.Value = lambda value: value
            layout = types.ModuleType('build_layout')
            layout.write_if_changed = Mock()
            with patch.dict(sys.modules, {'SCons.Script': script, 'build_layout': layout}):
                spec = importlib.util.spec_from_file_location('archive_provenance', ROOT/'scons/test_provenance.py')
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                module.register_test_provenance(env, 'build/client')
            generated, source, action = env.Command.call_args.args
            self.assertEqual(Path(generated), root/'build/client/include/glob2/TestBuildProvenance.h')
            with self.assertRaisesRegex(ValueError, 'requires a Git checkout'):
                action([], [], env)
