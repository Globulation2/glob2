"""Steam transfer archives fail closed before a game launch or upload."""

import hashlib
import importlib.util
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('steam_depot', ROOT / '.github/scripts/steam_depot.py')
depot = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(depot)
SHA = 'a' * 40


class SteamDepotTest(unittest.TestCase):
    def archive(self, root, *, marker=' ', source=SHA, checksum=None, extra=None):
        files = {'source-commit.txt': (source + '\n').encode(), 'data/file.txt': b'game data'}
        lines = []
        for name, content in files.items():
            digest = checksum or hashlib.sha256(content).hexdigest()
            lines.append(f'{digest} {marker}{name}\n')
        files['SHA256SUMS.txt'] = ''.join(lines).encode()
        if extra:
            files.update(extra)
        archive = root / 'windows.tar.gz'
        with tarfile.open(archive, 'w:gz') as stream:
            for name, content in files.items():
                member = tarfile.TarInfo('./' + name)
                member.size = len(content)
                stream.addfile(member, io.BytesIO(content))
        return archive

    def test_text_and_binary_checksum_markers(self):
        for marker in (' ', '*'):
            with self.subTest(marker=marker), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                depot.extract_verify(self.archive(root, marker=marker), root / 'depot', SHA)
                self.assertEqual((root / 'depot/data/file.txt').read_bytes(), b'game data')

    def test_wrong_source_corruption_and_unlisted_files_fail(self):
        for options in ({'source': 'b' * 40}, {'checksum': '0' * 64}, {'extra': {'unlisted': b'x'}}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaises(ValueError):
                    depot.extract_verify(self.archive(root, **options), root / 'depot', SHA)

    def test_archive_traversal_links_and_case_collisions_fail_before_extraction(self):
        for name, kind in (('../escape', tarfile.REGTYPE), ('/absolute', tarfile.REGTYPE),
                           ('C:/escape', tarfile.REGTYPE), ('data/link', tarfile.SYMTYPE),
                           ('data/link', tarfile.LNKTYPE), ('DATA/file.txt', tarfile.REGTYPE)):
            with self.subTest(name=name, kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = self.archive(root)
                # Rebuild the gzip archive with one adversarial member.
                with tarfile.open(archive, 'r:gz') as source:
                    members = [(m, source.extractfile(m).read()) for m in source.getmembers()]
                with tarfile.open(archive, 'w:gz') as stream:
                    for member, content in members:
                        stream.addfile(member, io.BytesIO(content))
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.linkname = '../../escape'
                    stream.addfile(member)
                with self.assertRaises(ValueError):
                    depot.extract_verify(archive, root / 'depot', SHA)
                self.assertFalse((root / 'depot').exists())

    def test_manifest_path_rejects_windows_and_parent_escape(self):
        for name in ('../escape', '/absolute', 'C:/escape', 'data\\escape', 'data/file:stream', 'data/file.'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                depot.relative_path(name)


if __name__ == '__main__':
    unittest.main()
