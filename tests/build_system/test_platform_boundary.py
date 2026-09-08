"""Dependency rules for the shared game and platform implementations."""
from pathlib import Path
import re
import unittest


class PlatformBoundaryTests(unittest.TestCase):
    def test_shared_code_does_not_embed_browser_api_calls(self):
        root = Path(__file__).resolve().parents[2]
        forbidden = re.compile(r'\b(?:EM_ASM\w*|EM_JS|emscripten_\w+)\s*\(|#\s*include\s*[<"]emscripten')
        for directory in ('src', 'libgag', 'libusl'):
            for path in (root / directory).rglob('*'):
                if path.suffix in ('.cpp', '.h'):
                    self.assertIsNone(forbidden.search(path.read_text()), str(path.relative_to(root)))

    def test_no_delay_macro_redefines_sdl_behavior(self):
        root = Path(__file__).resolve().parents[2]
        for directory in ('src', 'libgag', 'browser'):
            for suffix in ('*.cpp', '*.h'):
                for path in (root / directory).rglob(suffix):
                    self.assertNotRegex(path.read_text(), r'#\s*define\s+SDL_Delay\b', str(path))


if __name__ == '__main__':
    unittest.main()
