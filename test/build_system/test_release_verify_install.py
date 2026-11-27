import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("release_tool", ROOT / "tools/release/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

REQUIRED = (
    "usr/bin/glob2", "usr/share/applications/org.globulation2.Globulation2.desktop",
    "usr/share/metainfo/org.globulation2.Globulation2.metainfo.xml",
    "usr/share/icons/hicolor/128x128/apps/glob2.png",
    "usr/share/icons/hicolor/256x256/apps/glob2.png",
    "usr/share/glob2/data/fonts/sans.ttf",
    "usr/share/glob2/data/maxima/duel.strategy",
    "usr/share/glob2/data/usl/Language/Runtime/Control.usl",
    "usr/share/glob2/maps/SmallForTwo.map.gz",
)


class VerifyInstallTests(unittest.TestCase):
    def stage(self, root, *extra):
        for name in REQUIRED + extra:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"x")
        return root

    def verify(self, *extra):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(release, "packaged_assets", return_value=()), \
                patch.object(release, "reject_retired_assets"):
            release.verify_install(self.stage(Path(directory), *extra))

    def test_packed_resource_sheet_satisfies_the_resource_sprites(self):
        # Optimized exports pack ressource0.png into ressource.sheet (#1950).
        self.verify("usr/share/glob2/data/gfx/ressource.sheet")

    def test_unpacked_resource_frames_still_satisfy_it(self):
        self.verify("usr/share/glob2/data/gfx/ressource0.png")
        self.verify("usr/share/glob2/data/gfx/ressource0.webp")

    def test_missing_resource_sprites_fail(self):
        with self.assertRaisesRegex(SystemExit, "ressource.sheet"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
