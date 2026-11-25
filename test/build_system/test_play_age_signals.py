"""Play Age Signals is a Play-only, non-free dependency: it must stay out of F-Droid and Amazon builds."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PLAY = 'platforms/mobile/android-play'


class PlayAgeSignalsTests(unittest.TestCase):
    def test_fdroid_recipe_removes_the_play_directory_from_every_build(self):
        recipe = (ROOT / 'platforms/stores/fdroid/metadata/org.globulation2.glob2.yml').read_text()
        builds = re.split(r'^  - versionName: ', recipe, flags=re.MULTILINE)[1:]
        self.assertTrue(builds)
        for build in builds:
            removed = re.search(r'^    rm:\n((?:      - [^\n]+\n)+)', build, re.MULTILINE)
            self.assertIsNotNone(removed)
            self.assertIn('      - ' + PLAY + '\n', removed.group(1))

    def test_the_library_is_named_only_inside_the_play_directory(self):
        for path in (ROOT / 'platforms/mobile/android').rglob('*'):
            if path.is_file() and path.suffix in ('.gradle', '.java', '.kts', '.properties', '.xml'):
                self.assertNotIn('com.google.android.play', path.read_text(), str(path))
        self.assertIn("com.google.android.play:age-signals:", (ROOT / PLAY / 'play.gradle').read_text())

    def test_only_the_google_play_build_turns_it_on(self):
        source = (ROOT / 'platforms/mobile/android.py').read_text()
        self.assertIn("'play_age_signals':not (args.fdroid or args.amazon_apk or args.china)", source)


if __name__ == '__main__':
    unittest.main()
