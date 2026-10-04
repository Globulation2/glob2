# SPDX-License-Identifier: GPL-3.0-or-later
"""The web skin designer previews the same material shading the game renders."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BLOCK = re.compile(r'// BEGIN skin-material\n(.*?)// END skin-material', re.S)


def material_block(path):
    found = BLOCK.findall((ROOT / path).read_text())
    if len(found) != 1:
        raise AssertionError(f'{path}: expected one skin-material block, found {len(found)}')
    return found[0]


class SkinShaderParityTest(unittest.TestCase):
    def test_web_preview_matches_native_shader(self):
        self.assertEqual(material_block('libgag/src/GraphicContextSkinMesh.cpp'),
                         material_block('platform/apps/web/src/skins/MeshPreview.tsx'))


if __name__ == '__main__':
    unittest.main()
