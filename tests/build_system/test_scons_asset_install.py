"""Rebuild the real SCons release asset graph after a source file changes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('scons'), 'SCons command unavailable')
class SConsAssetInstallTests(unittest.TestCase):
    def test_rebuild_retains_ownership_and_removes_obsolete_installed_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'tools').symlink_to(ROOT/'tools', target_is_directory=True)
            (root/'scons').symlink_to(ROOT/'scons', target_is_directory=True)
            folder = root/'data/gfx';folder.mkdir(parents=True)
            original = folder/'frame0.png'
            Image.new('RGBA', (64,64), (20,80,130,255)).save(original)
            (root/'SConstruct').write_text(
                'import sys\n'
                'sys.path.insert(0, '+repr(str(ROOT))+')\n'
                'from scons.runtime_assets import install_assets\n'
                'env=Environment(tools=[], BUILDDIR="build", INSTALLDIR='+repr(str(root/'installed'))+')\n'
                'install_assets(env)\n')
            command = [shutil.which('scons'), '-Q', 'install']
            subprocess.run(command, cwd=root, check=True, capture_output=True, text=True)
            old = root/'installed/glob2/data/gfx/frame0.webp'
            self.assertTrue(old.is_file())
            original.rename(folder/'frame1.png')
            subprocess.run(command, cwd=root, check=True, capture_output=True, text=True)
            self.assertFalse(old.exists())
            self.assertTrue((root/'installed/glob2/data/gfx/frame1.webp').is_file())
            self.assertTrue((root/'build/runtime-assets.json').is_file())


if __name__ == '__main__':
    unittest.main()
