"""Handoffs must identify immutable published source bytes, never inferred success."""
import copy
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from tools.release import downstream


class DownstreamKitTests(unittest.TestCase):
    def fixture(self, root):
        archive = root / 'source.tar.gz'
        with tarfile.open(archive, 'w:gz') as source:
            data = b'PACKAGE_VERSION = "1.2.3"\n'
            entry = tarfile.TarInfo('glob2-1.2.3/scons/build_layout.py')
            entry.size = len(data)
            source.addfile(entry, io.BytesIO(data))
        release = dict(tag_name='v1.2.3', draft=False, prerelease=False, html_url='https://example.org/release',
            assets=[dict(name='glob2-1.2.3.tar.gz', size=archive.stat().st_size,
                         digest='sha256:' + hashlib.sha256(archive.read_bytes()).hexdigest(),
                         browser_download_url='https://example.org/source.tar.gz')])
        return archive, release

    def test_rejects_prereleases_and_changed_or_unidentified_archives(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, release = self.fixture(Path(directory))
            for field, value in [('draft', True), ('prerelease', True), ('tag_name', 'v4.5.6')]:
                invalid = dict(release, **{field: value})
                with self.subTest(field=field), self.assertRaises(ValueError):
                    downstream.verify_source('v1.2.3', archive, invalid, 'a'*40)
            invalid = copy.deepcopy(release)
            invalid['assets'][0]['digest'] = None
            with self.assertRaises(ValueError):
                downstream.verify_source('v1.2.3', archive, invalid, 'a'*40)
            archive.write_bytes(archive.read_bytes() + b'changed')
            with self.assertRaises(ValueError):
                downstream.verify_source('v1.2.3', archive, release, 'a'*40)

    def test_prepares_unsubmitted_campaign_and_refuses_to_overwrite_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, release = self.fixture(root)
            with patch.object(downstream, 'github', side_effect=[release, {'sha': 'b'*40}]):
                identity = downstream.prepare('v1.2.3', archive, 'coordinator', root/'kit')
            self.assertEqual(identity['sourceCommit'], 'b'*40)
            campaign = (root/'kit/campaign.md').read_text()
            for distro in downstream.DISTROS:
                self.assertIn('| ' + distro + ' | preparing |', campaign)
            self.assertIn('not submitted', campaign)
            with patch.object(downstream, 'github', side_effect=[release, {'sha': 'b'*40}]), self.assertRaises(ValueError):
                downstream.prepare('v1.2.3', archive, 'coordinator', root/'kit')
