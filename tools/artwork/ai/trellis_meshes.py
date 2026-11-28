#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage TRELLIS 3D meshes of HD building frames, for their ground shadows.

Staging only: nothing here writes the repository. For each building frame the
tool prepares the HD sprite as TRELLIS input (the base without the shadow the old
art baked in, `cast_shadows.strip`, with the team layer composited over it,
upscaled 2x), calls fal.ai's hosted `fal-ai/trellis` (Microsoft TRELLIS,
MIT-licensed weights) once per seed, and downloads the GLB mesh with a JSON
record of the request. `tools/artwork/mesh_shadows.py` then aligns the meshes to
the sprites, gates them, and casts the shadow masks that are committed under
`content-src/gfx/shadows/mesh/`; the meshes themselves are not committed.

A request costs about $0.02 and 25-35 s; frames and seeds run in parallel
(`--jobs`, default 8). The API key comes from `FAL_KEY` or `~/.config/fal/key`;
it is never printed or recorded.

Usage (needs the optional environment in requirements-mesh.txt):

    python tools/artwork/ai/trellis_meshes.py --all-buildings --output /tmp/glob2-meshes
    python tools/artwork/ai/trellis_meshes.py --frames hosp0b0 inn2b0 --seeds 2 3 4 \\
        --sprites /path/to/hd --output /tmp/glob2-meshes --append

Layout: `<output>/<frame>/input.png`, `<output>/<frame>/seed<N>.glb` and
`seed<N>.json` (endpoint, arguments, input and source hashes, mesh hash, request
id, timings), and `<output>/index.json`. A new run needs a fresh directory
outside the repository or under `artifacts/`; `--append` adds seeds or frames to
an existing staging directory.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/artwork"))
import cast_shadows  # noqa: E402

ENDPOINT = "fal-ai/trellis"
MODEL = "TRELLIS (microsoft/TRELLIS-image-large, MIT) via fal.ai"
INPUT_SCALE = 2
DEFAULT_SEEDS = (1,)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def building_frames(root=ROOT):
    """Every building frame with a shadow spec (resources excluded)."""
    return [f for f in cast_shadows.load_specs(root) if not f.startswith("ressource")]


def prepare(base, team, spec=None):
    """TRELLIS input: the base de-shadowed as cast_shadows.py does (spec `strip`,
    `paint_out`), team composited over it, upscaled 2x (PNG bytes)."""
    image = Image.fromarray(cast_shadows.deshadow(base, None, spec or {"strip": True})[0], "RGBA")
    if team is not None:
        layer = Image.new("RGBA", image.size)
        layer.paste(Image.fromarray(team, "RGBA").crop((0, 0) + image.size), (0, 0))
        image = Image.alpha_composite(image, layer)
    image = image.resize((image.width * INPUT_SCALE, image.height * INPUT_SCALE), Image.Resampling.LANCZOS)
    return cast_shadows._png(image)


def load_key():
    key = os.environ.get("FAL_KEY")
    if not key:
        path = Path.home() / ".config/fal/key"
        if not path.is_file():
            raise SystemExit("Set FAL_KEY or write the key to ~/.config/fal/key")
        key = path.read_text().strip()
    os.environ["FAL_KEY"] = key  # read by fal_client; never printed or recorded


def staging_output(output, append):
    output = Path(output).resolve()
    if ROOT.is_relative_to(output) or (output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "artifacts")):
        raise SystemExit("Use a staging directory outside the repository or under artifacts/")
    if output.exists() and not append:
        raise SystemExit("Staging output already exists; choose a fresh directory or pass --append")
    output.mkdir(parents=True, exist_ok=True)
    return output


def generate(frame, seed, input_path, sources, output):
    """One TRELLIS request; returns its record (written next to the GLB)."""
    import fal_client
    arguments = {"seed": seed}
    started = time.time()
    url = fal_client.upload_file(str(input_path))
    handle = fal_client.submit(ENDPOINT, arguments=dict(arguments, image_url=url))
    result = handle.get()
    mesh = result.get("model_mesh") or {}
    if not mesh.get("url"):
        raise RuntimeError(f"{frame} seed {seed}: no mesh in the response")
    with urllib.request.urlopen(mesh["url"]) as response:
        data = response.read()
    target = output / frame / f"seed{seed}.glb"
    target.write_bytes(data)
    record = {
        "frame": frame, "endpoint": ENDPOINT, "model": MODEL, "seed": seed, "arguments": arguments,
        "request_id": getattr(handle, "request_id", None), "input": "input.png",
        "input_sha256": sha(input_path.read_bytes()), "input_scale": INPUT_SCALE, "sources": sources,
        "mesh": target.name, "mesh_sha256": sha(data), "seconds": round(time.time() - started, 1),
        "timings": result.get("timings"),
    }
    (output / frame / f"seed{seed}.json").write_text(json.dumps(record, indent=1) + "\n")
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument("--frames", nargs="+")
    which.add_argument("--all-buildings", action="store_true", help="every building frame with a shadow spec")
    parser.add_argument("--sprites", type=Path, default=ROOT / "content/data/highres/v1",
                        help="HD frames <frame>.png and <frame>r.png (default: the committed pack)")
    parser.add_argument("--seeds", type=int, nargs="+", default=list(DEFAULT_SEEDS))
    parser.add_argument("--output", required=True)
    parser.add_argument("--append", action="store_true")
    parser.add_argument("--jobs", type=int, default=8, help="parallel requests")
    parser.add_argument("--specs", type=Path, help="shadow specs (default: the committed specs.json)")
    args = parser.parse_args(argv)
    specs = cast_shadows.load_specs(path=args.specs)
    frames = building_frames() if args.all_buildings else args.frames
    output = staging_output(args.output, args.append)
    load_key()
    jobs = []
    for frame in frames:
        base_path, team_path = args.sprites / f"{frame}.png", args.sprites / f"{frame}r.png"
        if not base_path.is_file():
            raise SystemExit(f"{frame}: no HD base in {args.sprites}")
        base = np.asarray(Image.open(base_path).convert("RGBA"))
        team = np.asarray(Image.open(team_path).convert("RGBA")) if team_path.is_file() else None
        (output / frame).mkdir(exist_ok=True)
        data = prepare(base, team, specs.get(frame))
        input_path = output / frame / "input.png"
        if input_path.exists() and input_path.read_bytes() != data:
            raise SystemExit(f"{frame}: its staged input differs from these sprites; use a fresh directory")
        input_path.write_bytes(data)
        sources = {"base_sha256": sha(base_path.read_bytes()),
                   "team_sha256": sha(team_path.read_bytes()) if team is not None else None}
        jobs += [(frame, seed, input_path, sources) for seed in args.seeds
                 if not (output / frame / f"seed{seed}.glb").exists()]
    print(f"{len(jobs)} TRELLIS requests (about ${0.02 * len(jobs):.2f})", flush=True)
    records, failures = [], []
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
        futures = {pool.submit(generate, *job, output): job for job in jobs}
        for future in concurrent.futures.as_completed(futures):
            frame, seed = futures[future][:2]
            try:
                record = future.result()
                records.append(record)
                print(f"  {frame} seed {seed}: {record['seconds']} s", flush=True)
            except Exception as error:  # noqa: BLE001 - keep the other requests going
                failures.append(f"{frame} seed {seed}: {type(error).__name__}: {str(error)[:200]}")
                print("  failed: " + failures[-1], flush=True)
    index_path = output / "index.json"
    index = json.loads(index_path.read_text()) if index_path.exists() else {"endpoint": ENDPOINT, "runs": []}
    index["runs"].append({"sprites": str(args.sprites), "requests": len(jobs), "succeeded": len(records),
                          "failures": failures, "seconds": round(time.time() - started, 1)})
    index_path.write_text(json.dumps(index, indent=1) + "\n")
    print(f"{len(records)} meshes in {time.time() - started:.0f} s; {len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
