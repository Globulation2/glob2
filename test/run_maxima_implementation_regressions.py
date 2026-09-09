#!/usr/bin/env python3
"""Run native Maxima regressions using an existing game build for engine objects.

Usage: python3 test/run_maxima_implementation_regressions.py --build-dir build
Recompiles Maxima, its AI factory and building lifetime tracking; leaves the game
binary and existing build objects untouched. Requires the game's pkg-config deps.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--test", action="append", choices=[
        "MaximaCombatIntegrationTest", "MaximaImplementationIntegrationTest",
        "MaximaFarmingIntegrationTest", "MaximaBarrierScenarioTest",
        "MaximaEconomyRegressionTest", "MaximaDirectorRegressionTest",
        "MaximaTacticsStandaloneTest", "MaximaPlacementStandaloneTest",
        "MaximaFarmingStandaloneTest", "MaximaDefenseStandaloneTest", "MaximaReconStandaloneTest", "MaximaStaffingStandaloneTest", "MaximaStrategyTest", "MaximaLifecycleTest"], help="Run only the named test (repeatable)")
    parser.add_argument("--placement-only", action="store_true",
                        help="Run placement/farming units and placement engine integration")
    parser.add_argument("--production-only", action="store_true",
                        help="Run swarm production integration and economy regressions")
    parser.add_argument("--reuse-built-objects", action="store_true", help="Use a freshly built engine's objects without recompiling Maxima")
    args = parser.parse_args()
    if args.placement_only:
        args.test = ["MaximaImplementationIntegrationTest",
                     "MaximaPlacementStandaloneTest", "MaximaFarmingStandaloneTest"]
    if args.production_only:
        args.test = ["MaximaImplementationIntegrationTest", "MaximaEconomyRegressionTest"]
    build = (ROOT / args.build_dir).resolve()
    packages = ["sdl2", "SDL2_net", "SDL2_ttf", "SDL2_image", "vorbisfile",
                "speex", "fribidi", "epoxy"]
    cflags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", *packages], text=True))
    libs = shlex.split(subprocess.check_output(
        ["pkg-config", "--libs", *packages], text=True))
    libs += ["-lboost_date_time", "-lpthread", "-lz"]
    if sys.platform == "darwin":
        libs += ["-framework", "OpenGL", "-framework", "GLUT"]
    elif sys.platform.startswith("linux"):
        libs += ["-lGL", "-lGLU"]
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=gnu++20", "-O1", "-UNDEBUG", "-I.", "-Isrc",
             "-Ilibgag/include", "-Ilibusl/src", *["-I"+str(p) for p in (ROOT/"src").rglob("*") if p.is_dir()], *cflags]
    sources = (ROOT / "src/SConscript").read_text().split('"""')[1].split()
    rebuilt = [s for s in sources if s.startswith("AIMaxima") or s in ("ai/AI.cpp", "building/Lifecycle.cpp")]
    objects = [build / "src" / Path(s).with_suffix(".o") for s in sources
               if s not in rebuilt and s != "Glob2.cpp"]
    objects.extend([build/"libgag/src/libgag.a",build/"libusl/src/libusl.a"])
    missing = [str(p) for p in objects if not p.is_file()]
    if missing:
        parser.error("Build the game first; missing objects: " + ", ".join(missing))
    with tempfile.TemporaryDirectory(prefix="maxima-regressions-") as directory:
        temporary = Path(directory)
        for source in rebuilt:
            obj = temporary / Path(source).with_suffix(".o")
            obj.parent.mkdir(parents=True,exist_ok=True)
            if args.reuse_built_objects:
                obj.symlink_to(build/"src"/Path(source).with_suffix(".o"))
            else:
                run([*compiler, *flags, "-c", "src/" + source, "-o", str(obj)])
            objects.append(obj)
        sha1 = temporary / "sha1.o"
        run([*compiler, *flags, "-x", "c++", "-c", "gnupg/sha1.c", "-o", str(sha1)])
        for name, modules in (
            ("MaximaCombatIntegrationTest", objects),
            ("MaximaImplementationIntegrationTest", objects),
            ("MaximaFarmingIntegrationTest", objects),
            ("MaximaBarrierScenarioTest", objects),
            ("MaximaEconomyRegressionTest", objects),
            ("MaximaDirectorRegressionTest", objects),
            ("MaximaDefenseStandaloneTest", [temporary / "AIMaximaDefense.o"]),
            ("MaximaReconStandaloneTest", [temporary / "AIMaximaRecon.o"]),
            ("MaximaStaffingStandaloneTest", []),
            ("MaximaStrategyTest", objects),
            ("MaximaLifecycleTest", objects),
            ("MaximaTacticsStandaloneTest", [temporary / "AIMaximaTactics.o"]),
            ("MaximaFarmingStandaloneTest", [temporary / "AIMaximaFarming.o"]),
            ("MaximaPlacementStandaloneTest", [temporary / "AIMaximaPlacement.o", sha1,
                *[build/"libgag/src"/n for n in ("Stream.o", "StreamBackend.o", "BinaryStream.o")]]),
        ):
            if args.test and name not in args.test:
                continue
            binary = temporary / name
            run([*compiler, *flags, "test/" + name + ".cpp",
                 *map(str, modules), *libs, "-o", str(binary)])
            arguments = ["--placement-only"] if (args.placement_only
                and name == "MaximaImplementationIntegrationTest") else []
            if args.production_only and name == "MaximaImplementationIntegrationTest":
                arguments = ["--production-only"]
            result = subprocess.run([str(binary), *arguments], cwd=ROOT, capture_output=True, text=True,
                                    env=dict(os.environ, TMPDIR=str(temporary), TEMP=str(temporary), TMP=str(temporary)))
            if result.returncode:
                sys.stderr.write(result.stdout + result.stderr)
                result.check_returncode()
            print(name + (" (" + arguments[0][2:].replace("-", " ") + ")" if arguments else "") + ": PASS", flush=True)


if __name__ == "__main__":
    main()
