#!/usr/bin/env python3
"""Cross-surface guardrails for the public Maxima name."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MaximaNamingTest(unittest.TestCase):
    def test_ui_uses_dedicated_name_and_description_keys(self) -> None:
        names = (ROOT / "src/ai/AINames.cpp").read_text()
        keys = (ROOT / "data/texts.keys.txt").read_text().splitlines()
        english = (ROOT / "data/texts.en.txt").read_text()
        # One table row drives the CLI name and both localized keys.
        self.assertRegex(names, r'\{AI::MAXIMA,\s*"maxima",\s*"AIMaxima"')
        self.assertIn("[AIMaxima]", keys)
        self.assertIn("[AIMaxima-Description]", keys)
        self.assertIn("[AIMaxima]\nMaxima\n", english)
        self.assertIn("[AIMaxima-Description]\nAn adaptive AI", english)

    def test_configuration_and_build_paths_are_renamed(self) -> None:
        self.assertTrue((ROOT / "data/maxima/base.strategy").is_file())
        self.assertFalse((ROOT / "data/nicowar-v3").exists())
        manifests = "\n".join(
            (ROOT / name).read_text()
            for name in (
                "SConstruct",
                "src/SConscript",
                "test/SConstruct",
            )
        )
        self.assertIn("AIMaxima.cpp", manifests)
        self.assertNotIn("AINicowarV3.cpp", manifests)

    def test_active_source_and_tool_filenames_use_maxima(self) -> None:
        stale = []
        for directory in ("src", "test", "tools", "data", "doc"):
            for path in (ROOT / directory).rglob("*"):
                if not path.is_file() or "__pycache__" in path.parts:
                    continue
                if any(token in path.name for token in (
                    "NicowarV3", "nicowar_v3", "nicowar-v3"
                )):
                    stale.append(str(path.relative_to(ROOT)))
        self.assertEqual(stale, [])

    def test_retired_nicowar_variants_are_absent(self) -> None:
        retired = (
            ROOT / "src/AINicowarV2.cpp",
            ROOT / "src/AINicowarV2.h",
            ROOT / "src/AINicowarV4.cpp",
            ROOT / "src/AINicowarV4.h",
            ROOT / "data/nicowar-v2.default.txt",
        )
        self.assertFalse(any(path.exists() for path in retired))
        active = "\n".join(
            (ROOT / path).read_text()
            for path in (
                "src/ai/AI.h",
                "src/ai/AI.cpp",
                "src/ai/AINames.cpp",
                "src/SConscript",
            )
        )
        self.assertNotIn("AINicowarV2", active)
        self.assertNotIn("AINicowarV4", active)


if __name__ == "__main__":
    unittest.main()
