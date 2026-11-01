"""Exercise release-source boundaries with real temporary Git histories."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('epic_source_ref', Path(__file__).resolve().parents[1] / '.github/scripts/epic_source_ref.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class EpicSourceRefTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        previous = os.getcwd()
        os.chdir(self.temp.name)
        self.addCleanup(os.chdir, previous)
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Fixture')
        for name in module.ALLOWED_PATHS | {'scons/build_layout.py', 'src/game/example.cpp'}:
            path = Path(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('release\n')
        self.commit()
        self.base = self.git('rev-parse', 'HEAD')
        self.git('tag', 'v0.11.4.0')

    def git(self, *args):
        return subprocess.check_output(['git', *args], stderr=subprocess.DEVNULL, text=True).strip()

    def commit(self):
        self.git('add', '-A')
        self.git('commit', '-qm', 'fixture')
        return self.git('rev-parse', 'HEAD')

    def select(self, sha):
        self.git('checkout', '--detach', self.base)
        return module.select_source('v0.11.4.0', sha, self.temp.name)

    def test_default_remains_exact_tag(self):
        self.assertEqual(self.base, self.select(''))

    def test_cli_patch_allowed(self):
        Path('src/app/cli/CommandLine.cpp').write_text('fixed\n')
        sha = self.commit()
        self.assertEqual(sha, self.select(sha))
        self.assertEqual(sha, self.git('rev-parse', 'HEAD'))

    def test_mutable_or_invalid_reference_rejected(self):
        for value in ['master', 'v0.11.4.0', self.base.upper(), 'bad\nref']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.select(value)

    def test_unrelated_engine_and_version_changes_rejected(self):
        for path in ['src/game/example.cpp', 'scons/build_layout.py']:
            self.git('checkout', '--detach', self.base)
            Path(path).write_text('changed\n')
            sha = self.commit()
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.select(sha)

    def test_no_patch_rejected(self):
        with self.assertRaises(ValueError):
            self.select(self.base)

    def test_deleted_or_symlink_file_rejected(self):
        path = Path('src/app/cli/CommandLine.cpp')
        for symlink in [False, True]:
            self.git('checkout', '--detach', self.base)
            path.unlink()
            if symlink:
                path.symlink_to('CommandLineTest.cpp')
            sha = self.commit()
            with self.subTest(symlink=symlink), self.assertRaises(ValueError):
                self.select(sha)

    def test_preflight_must_start_at_tag(self):
        Path('src/app/cli/CommandLine.cpp').write_text('fixed\n')
        sha = self.commit()
        with self.assertRaises(ValueError):
            module.select_source('v0.11.4.0', sha, self.temp.name)

    def test_executable_file_rejected(self):
        Path('src/app/cli/CommandLine.cpp').chmod(0o755)
        sha = self.commit()
        with self.assertRaises(ValueError):
            self.select(sha)

    def test_non_descendant_rejected(self):
        self.git('checkout', '--orphan', 'unrelated')
        Path('src/app/cli/CommandLine.cpp').write_text('unrelated\n')
        sha = self.commit()
        with self.assertRaises(subprocess.CalledProcessError):
            self.select(sha)


if __name__ == '__main__':
    unittest.main()
