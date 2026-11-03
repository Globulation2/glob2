"""The trial must materialize a newer candidate from the reviewed recipe."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class RecipeTrialTests(unittest.TestCase):
    def run_trial(self, recipe):
        workflow = (ROOT / '.github/workflows/fdroid-buildserver-trial.yml').read_text()
        script = workflow.split("python3 - <<'PY'\n", 1)[1].split('          PY', 1)[0]
        script = '\n'.join(line[10:] for line in script.splitlines())
        with tempfile.TemporaryDirectory() as task:
            root = Path(task)
            (root / 'metadata').mkdir()
            (root / 'platforms/mobile').mkdir(parents=True)
            path = root / 'metadata/org.globulation2.glob2.yml'
            path.write_text(recipe)
            (root / 'platforms/mobile/android-release.json').write_text(json.dumps({
                'versionName': '0.12.0.1', 'versionCodeBase': 120001}))
            result = subprocess.run([sys.executable, '-c', script], cwd=root,
                env=dict(os.environ, SOURCE_COMMIT='a' * 40), capture_output=True, text=True)
            return result, path.read_text()

    def test_rewrites_all_three_abis_and_update_identity(self):
        recipe = (ROOT / 'platforms/stores/fdroid/metadata/org.globulation2.glob2.yml').read_text()
        result, text = self.run_trial(recipe)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(re.findall(r'^    versionCode: (\d+)$', text, re.M),
            ['1200011', '1200012', '1200013'])
        self.assertEqual(text.count('versionName: 0.12.0.1'), 3)
        self.assertEqual(text.count('commit: ' + 'a' * 40), 3)
        self.assertIn('CurrentVersion: 0.12.0.1', text)
        self.assertIn('CurrentVersionCode: 1200013', text)

    def test_rejects_missing_or_duplicate_abis(self):
        recipe = (ROOT / 'platforms/stores/fdroid/metadata/org.globulation2.glob2.yml').read_text()
        for replacement in ('1100002', '1100004'):
            with self.subTest(replacement=replacement):
                result, _ = self.run_trial(recipe.replace('versionCode: 1100003',
                    'versionCode: ' + replacement))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('one recipe entry for each ABI', result.stderr)

if __name__ == '__main__':
    unittest.main()
