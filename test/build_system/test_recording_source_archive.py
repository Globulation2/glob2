"""Release sources retain the exact pinned codec archives for offline builds."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'scons'))
from tools.release import release

class RecordingSourceArchiveTest(unittest.TestCase):
    def test_pinned_archives_append_after_reading_git_archive_and_are_repeatable(self):
        prefix='glob2-1.2.3/'
        dependency=b'pinned encoder source'
        pins={'x264':{'archive':'x264.tar.gz','sha256':hashlib.sha256(dependency).hexdigest()}}
        stream=io.BytesIO()
        required=('SConstruct','scons/build_layout.py','data/glob2.desktop',
                  'data/org.globulation2.Globulation2.metainfo.xml','data/screenshots/globulation2-gameplay.png',
                  'data/usl/Language/Runtime/Control.usl','data/fonts/sans.ttf','maps/SmallForTwo.map.gz')
        with tarfile.open(fileobj=stream,mode='w') as archive:
            for name in (*required,'scons/recording-versions.json'):
                data=json.dumps(pins).encode() if name.endswith('recording-versions.json') else b'source'
                entry=tarfile.TarInfo(prefix+name);entry.size=len(data);archive.addfile(entry,io.BytesIO(data))
        def download(directory,versions):
            for spec in versions.values(): (directory/spec['archive']).write_bytes(dependency)
        with tempfile.TemporaryDirectory() as directory,patch.object(release,'validate',return_value='1.2.3'),patch.object(release.subprocess,'check_output',return_value=stream.getvalue()),patch('sdl3_dependencies.download',download):
            target=release.archive(Path(directory));first=target.read_bytes()
            self.assertEqual(first,release.archive(Path(directory)).read_bytes())
            with tarfile.open(target,'r:gz') as archive:
                self.assertEqual(archive.extractfile(prefix+'third_party/recording-sources/x264.tar.gz').read(),dependency)
