#!/usr/bin/env python3
"""Structural acceptance tests for the standalone Maxima runtime."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAXIMA_FILES = [
    ROOT / "src/AIMaxima.h",
    ROOT / "src/AIMaxima.cpp",
    ROOT / "src/AIMaximaRecon.h",
    ROOT / "src/AIMaximaRecon.cpp",
    ROOT / "src/AIMaximaRuntime.h",
    ROOT / "src/AIMaximaRuntime.cpp",
]


class MaximaDecouplingTest(unittest.TestCase):
    def test_runtime_sources_have_no_echo_dependency(self):
        text = "\n".join(path.read_text() for path in MAXIMA_FILES)
        for forbidden in ("AIEcho", "EchoAI", "boost::logic", "tribool"):
            self.assertNotIn(forbidden, text)

    def test_direct_ai_and_typed_runtime_contracts(self):
        header = (ROOT / "src/AIMaxima.h").read_text()
        runtime = (ROOT / "src/AIMaximaRuntime.h").read_text()
        implementation = (ROOT / "src/AIMaximaRuntime.cpp").read_text()
        self.assertIn("public AIImplementation", header)
        self.assertIn("RuntimeEvent", runtime)
        self.assertIn("PlacementResult", runtime)
        self.assertIn("if(!placement.found)", implementation)
        self.assertIn("seenByMask", implementation)
        self.assertIn("queuedIndexes", runtime)

    def test_built_objects_have_no_undefined_echo_symbols(self):
        objects = [
            ROOT / "build-tournament/src/AIMaxima.o",
            ROOT / "build-tournament/src/AIMaximaRecon.o",
            ROOT / "build-tournament/src/AIMaximaRuntime.o",
        ]
        if not all(path.exists() for path in objects):
            self.skipTest("optimized Maxima objects have not been built")
        output = subprocess.check_output(["nm", "-u", *map(str, objects)], text=True)
        self.assertNotIn("AIEcho", output)
        self.assertNotIn("EchoAI", output)


if __name__ == "__main__":
    unittest.main()
