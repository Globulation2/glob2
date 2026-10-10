# SPDX-License-Identifier: GPL-3.0-or-later
"""The colony-skin material registry agrees with its shader, docs and consumers."""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import skin_materials  # noqa: E402


class SkinMaterialsTest(unittest.TestCase):
    def test_registry_and_shader_agree(self):
        registry = skin_materials.load_registry(ROOT)
        skin_materials.check_shader(ROOT, registry)
        header = skin_materials.header_text(ROOT)
        self.assertIn('#define SKIN_MATERIAL_COUNT %d' % len(registry['materials']), header)
        self.assertIn('#define SKIN_MATERIAL_SHELLS %d' % registry['shells'], header)
        self.assertIn('R"GLSL(', header)

    def test_shader_stays_portable(self):
        """Both GLSL 1.20 and ES 3.00 must compile the shared block as is."""
        shader = (ROOT / skin_materials.SHADER).read_text()
        code = re.sub(r'//[^\n]*', '', shader)
        self.assertNotIn('#version', code)
        # Lookups go through the wrapper-defined macro, never a dialect's name.
        self.assertNotRegex(code, r'\btexture2?D?\s*\(')
        self.assertNotRegex(shader, r'(?m)^\s*(for|while)\s*\(', 'materials stay loop-free for GL 2.1 drivers')
        for line in shader.splitlines():
            if line.startswith('#define'):
                self.assertFalse(line.endswith('\\'), 'GLSL 1.20 has no line continuation')

    def test_shader_edits_rebake_sprites(self):
        text = (ROOT / 'scons/skin_render.py').read_text()
        for path in (skin_materials.REGISTRY, skin_materials.SHADER):
            self.assertIn("'%s'" % path.as_posix(), text, '%s must feed SKIN_RENDER_REVISION' % path)

    def test_web_studio_imports_the_shader(self):
        module = (ROOT / 'platform/apps/web/src/skins/materialShader.ts').read_text()
        self.assertIn("libgag/shaders/skin-material.glsl?raw'", module)
        self.assertIn('COPY libgag/shaders', (ROOT / 'deploy/Dockerfile').read_text())

    def test_protocol_mirror(self):
        """The platform ships a copy of the registry; its vitest pins equality,
        this guards the copy from drifting in checkouts without node."""
        registry = json.loads((ROOT / skin_materials.REGISTRY).read_text())
        text = (ROOT / 'platform/packages/protocol/src/skins.ts').read_text()
        for material in registry['materials']:
            self.assertRegex(text, r"\{ id: %d, key: '%s', name: '%s', group: '%s', shells: %s \}" % (
                material['id'], material['key'], re.escape(material['name']), material['group'],
                'true' if material['shells'] else 'false'))
        self.assertIn('COLONY_SKIN_SHELLS = %d;' % registry['shells'], text)
        self.assertIn('COLONY_SKIN_FUR_LENGTH = %s;' % registry['furLength'], text)
        self.assertIn('COLONY_SKIN_SHELL_DEPTH = %s;' % registry['shellDepth'], text)

    def test_docs_point_at_the_registry(self):
        for path in ('docs/multiplayer/colony-skins.md', 'docs/architecture/rendering.md',
                     'tools/unit-animation/README.md'):
            self.assertIn('skin-materials.json', (ROOT / path).read_text(), path)


if __name__ == '__main__':
    unittest.main()
