"""Mock the cloud boundary while verifying a real localized static package."""
import gzip
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
import test_browser_package as fixtures

spec = importlib.util.spec_from_file_location('publish_static', fixtures.module.ROOT / 'browser/publish-static.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class BrowserPublishTests(unittest.TestCase):
    def setUp(self):
        fixture = fixtures.BrowserPackageTests(methodName='runTest')
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.threaded_source()
        fixture.localized_source()
        # Keep both runtime variants and actual Telugu content in the fixture.
        html = fixture.source / 'index.html'
        html.write_text(html.read_text().replace('<script src="index.js">', '<script src="loader.js">')
                        + '<link rel="stylesheet" href="script-fonts.css">')
        (fixture.source / 'locales/te.json').write_bytes(
            (fixtures.module.ROOT / 'platform/packages/i18n/locales/te.json').read_bytes())
        sys.path.insert(0, str(fixtures.module.ROOT / 'scons'))
        import web_assets
        web_assets.launcher_script_fonts(fixtures.module.ROOT, fixture.source)
        self.version = fixtures.module.package(fixture.source, fixture.output)
        self.output = fixture.output

    def test_uploads_complete_localization_with_mime_gzip_cache_and_html_last(self):
        with patch.object(publisher.subprocess, 'run') as run:
            publisher.publish(self.output, 'gs://fixture-bucket')
        commands = [call.args[0] for call in run.call_args_list]
        by_name = {command[4].removeprefix('gs://fixture-bucket/'): command for command in commands}
        self.assertEqual(commands[-1][4], 'gs://fixture-bucket/index.html')
        expected = {p.relative_to(self.output).as_posix()[:-3] for p in self.output.rglob('*.gz')}
        self.assertEqual(set(by_name), expected)
        for name, mime in [('locales/te.json', 'application/json'), ('script-fonts.css', 'text/css'),
                           ('fonts/NotoSansTelugu-Regular.ttf', 'font/ttf'),
                           ('fonts/LICENSE-Noto.txt', 'text/plain'), ('i18n.js', 'text/javascript')]:
            with self.subTest(name=name):
                command = by_name[name]
                self.assertIn('--content-type=' + mime, command)
                self.assertIn('--cache-control=' + publisher.REVALIDATE, command)
                self.assertIn('--content-encoding=gzip', command)
                self.assertEqual(gzip.decompress(Path(command[3]).read_bytes()), (self.output / name).read_bytes())
        for name in [f'index-{self.version}.js', f'index-{self.version}.wasm',
                     f'threaded/index-{self.version}.wasm', 'assets/core.0123456789abcdef.data']:
            self.assertIn('--cache-control=' + publisher.IMMUTABLE, by_name[name])
        workflow = (fixtures.module.ROOT / '.github/workflows/browser-release.yml').read_text()
        publish_job = workflow.split('\n  publish:\n', 1)[1]
        self.assertLess(publish_job.index('uses: actions/checkout@'), publish_job.index('google-github-actions/auth@'))
        self.assertIn('run: python3 browser/publish-static.py candidate "$BUCKET"', publish_job)

    def test_corrupt_candidate_is_rejected_before_any_upload(self):
        (self.output / 'locales/te.json').write_text('{}')
        with patch.object(publisher.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                publisher.publish(self.output, 'gs://fixture-bucket')
        run.assert_not_called()

    def test_failed_asset_upload_does_not_publish_entry_point(self):
        with patch.object(publisher.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'gcloud')) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                publisher.publish(self.output, 'gs://fixture-bucket')
        self.assertEqual(run.call_count, 1)
        self.assertNotEqual(run.call_args.args[0][4], 'gs://fixture-bucket/index.html')


if __name__ == '__main__':
    unittest.main()
