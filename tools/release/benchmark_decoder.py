#!/usr/bin/env python3
"""Compare stock/private SDL_image against the same exported image files."""

import argparse
import ctypes
import ctypes.util
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.release.benchmark_profiles import repeatable_slowdown


def decode(assets, runtime, image_library=None):
    # Each measurement runs in a fresh process: Windows must never reuse a DLL
    # of the same name already loaded from the other runtime's directory.
    directory = (
        os.add_dll_directory(str(runtime.resolve())) if os.name == "nt" else None
    )
    sdl = ctypes.CDLL(
        str(runtime / "SDL2.dll")
        if os.name == "nt"
        else ctypes.util.find_library("SDL2")
    )
    library = image_library or (
        str(runtime / "SDL2_image.dll")
        if os.name == "nt"
        else ctypes.util.find_library("SDL2_image")
    )
    image = ctypes.CDLL(str(library))
    image.IMG_Init.argtypes = [ctypes.c_int]
    image.IMG_Init.restype = ctypes.c_int
    image.IMG_Load.argtypes = [ctypes.c_char_p]
    image.IMG_Load.restype = ctypes.c_void_p
    sdl.SDL_FreeSurface.argtypes = [ctypes.c_void_p]
    sdl.SDL_FreeSurface.restype = None
    sdl.SDL_GetError.restype = ctypes.c_char_p
    if image.IMG_Init(11) & 11 != 11:
        raise ValueError("Decoder must initialize PNG, JPEG and WebP")
    files = sorted(
        path
        for path in assets.rglob("*")
        if path.is_file() and path.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp")
    )
    if not files:
        raise ValueError("No packaged images found")
    started = time.perf_counter()
    for path in files:
        surface = image.IMG_Load(os.fsencode(path))
        if not surface:
            raise ValueError(f"Cannot decode {path}: {sdl.SDL_GetError()}")
        sdl.SDL_FreeSurface(surface)
    elapsed = time.perf_counter() - started
    if directory:
        directory.close()
    return dict(images=len(files), seconds=elapsed, image_library=str(library))


def benchmark(
    assets, baseline, candidate, output, baseline_library=None, candidate_library=None
):
    rows, pairs = [], []
    for batch in range(2):
        for repeat in range(-2, 7):
            measured = {}
            order = (
                ("baseline", "candidate")
                if repeat % 2 == 0
                else ("candidate", "baseline")
            )
            for variant in order:
                runtime = baseline if variant == "baseline" else candidate
                library = (
                    baseline_library if variant == "baseline" else candidate_library
                )
                command = [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--worker",
                    "--assets",
                    str(assets.resolve()),
                    "--runtime",
                    str(runtime.resolve()),
                ]
                if library:
                    command += ["--image-library", str(library)]
                result = json.loads(subprocess.check_output(command, text=True))
                result.update(batch=batch, repeat=repeat, variant=variant)
                rows.append(result)
                measured[variant] = result
            if measured["baseline"]["images"] != measured["candidate"]["images"]:
                raise ValueError("Decoder comparison did not load the same images")
            if repeat >= 0:
                pairs.append(
                    (
                        batch,
                        measured["baseline"]["seconds"],
                        measured["candidate"]["seconds"],
                    )
                )
    result = dict(
        measurements=rows,
        repeated_slowdown=repeatable_slowdown(pairs),
        scope="SDL_image CPU decoding of identical packaged images; GPU upload/rendering excluded",
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    if result["repeated_slowdown"]:
        raise ValueError(
            "Private decoder has a statistically credible slowdown in both batches"
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--image-library")
    parser.add_argument("--baseline-runtime", type=Path)
    parser.add_argument("--candidate-runtime", type=Path)
    parser.add_argument("--baseline-library")
    parser.add_argument("--candidate-library")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.worker:
        if not args.runtime:
            parser.error("--worker requires --runtime")
        print(json.dumps(decode(args.assets, args.runtime, args.image_library)))
    else:
        if not all((args.baseline_runtime, args.candidate_runtime, args.output)):
            parser.error("comparison requires both runtime directories and --output")
        benchmark(
            args.assets,
            args.baseline_runtime,
            args.candidate_runtime,
            args.output,
            args.baseline_library,
            args.candidate_library,
        )


if __name__ == "__main__":
    main()
