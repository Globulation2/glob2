#!/usr/bin/env python3
"""Rebuild the recorded pre-change thumbnail and selection pipeline locally."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
REFERENCE = "a9c5c13118eabb910a1055f9baa417bf5513aa92"
os.chdir(ROOT)
options = ["release=1", "server=0", "map-preview-test"]
subprocess.run(["scons", "-j6", *options], check=True)
# Ask SCons for these two actions without rebuilding the engine. Only generated
# harness outputs are temporarily moved, then restored even if the dry run fails.
with tempfile.TemporaryDirectory(prefix="preview-command-plan-", dir=ROOT / "build") as backup:
    moved = []
    try:
        for name in ("MapPreviewHarness.o", "MapPreviewHarness"):
            source, destination = ROOT / "build/src" / name, Path(backup) / name
            os.replace(source, destination)
            moved.append((source, destination))
        plan = subprocess.check_output(["scons", "-n", *options], text=True)
    finally:
        for source, destination in moved:
            os.replace(destination, source)
commands = [shlex.split(line) for line in plan.splitlines() if " -o " in line]
compile_command = next(c for c in commands if "test/MapPreviewHarness.cpp" in c and "-c" in c)
link_command = next(c for c in commands if "build/src/MapPreviewHarness" in c and "-c" not in c)
with tempfile.TemporaryDirectory(prefix="glob2-preview-baseline-") as directory:
    work = Path(directory)
    for suffix in ("h", "cpp"):
        original = subprocess.check_output(
            ["git", "show", f"{REFERENCE}:src/map/io/MapThumbnail.{suffix}"], text=True)
        (work / f"LegacyMapThumbnail.{suffix}").write_text(
            original.replace("MapThumbnail", "LegacyMapThumbnail"))
    (work / "Baseline.cpp").write_text((EVIDENCE / "Baseline.cpp").read_text())
    for name in ("LegacyMapThumbnail", "Baseline"):
        command = [str(work / f"{name}.cpp") if a == "test/MapPreviewHarness.cpp" else
                   str(work / f"{name}.o") if a == "build/src/MapPreviewHarness.o" else a
                   for a in compile_command]
        subprocess.run(command, check=True)
    command = []
    for arg in link_command:
        if arg == "build/src/MapPreviewHarness.o":
            command.extend([str(work / "Baseline.o"), str(work / "LegacyMapThumbnail.o")])
        elif arg == "build/src/MapPreviewHarness":
            command.append(str(work / "Baseline"))
        else:
            command.append(arg)
    subprocess.run(command, check=True)
    subprocess.run([str(work / "Baseline"), str(EVIDENCE / "selection-fixture.map")], check=True)
