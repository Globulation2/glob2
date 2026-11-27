# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure the size/quality curves the asset packer picks sprite settings from.

For every packed sprite group (native terrain families and unit clips, and HD
groups) this encodes lossless WebP, lossy WebP at each quality in
packed_sprite_assets.WEBP_GRID and, for groups that admit video, H.264 at each
CRF in CRF_GRID; it records stored bytes, browser transfer bytes and the
SSIMULACRA2 score of each, in tools/sprite-quality-curves.json. Exports then
choose a point per group instantly for their target and available codecs
(docs/development/package-size.md#sprite-quality-curves).

Entries whose sources, encoders and metric still match are kept, so a rerun only
measures what changed. --check reports stale or missing groups without encoding.
"""

import argparse
import copy
from concurrent.futures import ProcessPoolExecutor, as_completed
import fnmatch
import json
import os
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import package_assets as images  # noqa: E402


def sprite_groups():
    """{"native/<name>" | "hd/<name>": group} as a standard optimized export plans them,
    plus "<scope>/<name>@<edition>" for each edition group whose frames differ
    (lite's trimmed terrain), so every edition picks from curves of its own frames."""
    import importlib.util
    from tools import packed_sprite_assets as packed

    content = ROOT / images.CONTENT
    spec = importlib.util.spec_from_file_location("terrain_tileset", ROOT / "tools/terrain_tileset.py")
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    terrain_document = compiler.terrain_catalog.load(content)
    terrain_sprites = {material["sprite"] for material in terrain_document["materials"]
                       if material["sprite"].startswith("data/gfx/terrain-")}

    materials = {material["sprite"] for material in terrain_document["materials"]}

    def native(platform, variant_blocks=0, fold=False, single_colour_decor=False):
        files = list(images.source_files(ROOT, platform))
        renames = {}
        document = terrain_document
        if variant_blocks or fold or single_colour_decor:
            # small_build_terrain edits the catalog in place, as an edition's export
            # does; work on a copy so the standard groups keep their own materials.
            document = copy.deepcopy(terrain_document)
            _, dropped, renames = images.small_build_terrain(
                document, content, fold=fold, single_colour_decor=single_colour_decor,
                variant_blocks=variant_blocks, packed=images.sprite_families(files, content))
            files = [f for f in files if f.relative_to(content).as_posix() not in dropped]
        sheets = images.sheet_plan(files, content, terrain_sprites, (), renames, materials)
        return {name: group for by_sprite in images.native_sprite_groups(sheets, terrain_sprites, document).values()
                for name, group in by_sprite.items()}

    groups = {"native/" + name: group for name, group in native("generic").items()}
    _, hd = packed.plan(content, terrain_document)
    groups.update({"hd/" + name: group for name, group in hd.items()})
    # Editions' own frames (buildsys/edition.py presets; lite has no HD groups).
    sys.path.insert(0, str(ROOT / "buildsys"))
    import edition
    import web_assets
    for name, preset in edition.PRESETS.items():
        if name == "standard":
            continue
        variant = native("web", preset["variant_blocks"], preset["terrain_fallbacks"],
                         preset["single_colour_decor"])
        for group_name, group in variant.items():
            standard = groups.get("native/" + group_name)
            if standard is None or packed.group_identity(standard) != packed.group_identity(group):
                groups[f"native/{group_name}@{name}"] = group
    return groups


def current(entry, group):
    from tools import packed_sprite_assets as packed

    video = group["crf"] is not None
    return (entry is not None and entry.get("group") == packed.group_identity(group)
            and set(entry.get("webp", {})) == {str(q) for q in packed.WEBP_GRID}
            and set(entry.get("h264", {})) == ({packed.crf_key(c) for c in packed.CRF_GRID} if video else set())
            and entry.get("webp_encoder") == packed.webp_identity()
            and entry.get("metric") == packed.metric_identity()
            and entry.get("h264_encoder") == (packed.video_identity() if group["crf"] is not None else None))


def sprite_ffmpeg(jobs):
    """The pinned sprite encoder, built once into the shared asset cache."""
    sys.path.insert(0, str(ROOT / "buildsys"))
    from dev_store import Lease, cache as store_cache
    from recording_dependencies import build
    directory = store_cache(ROOT, "asset-cache") / "host-sprite-encoder"
    with Lease(directory, exclusive=True, track_use=False):
        prefix = build(directory / "prefix", directory / "sources", jobs=jobs, sprite_tool=True)
    return str(prefix / "bin" / ("ffmpeg.exe" if os.name == "nt" else "ffmpeg"))


def _gate_scoring(semaphore):
    """Pool initializer: scoring a 2048x2048 mosaic peaks at about 0.8 GB with libjxl,
    above an encode, so only `--scorers` workers score at once while the rest encode.
    A worker also exits if its parent dies, so a killed run leaves no workers behind."""
    import threading
    import time
    from tools import packed_sprite_assets as packed

    parent = os.getppid()

    def orphan_watch():
        while os.getppid() == parent:
            time.sleep(2)
        os._exit(1)
    threading.Thread(target=orphan_watch, daemon=True).start()
    score = packed.perceptual_score

    def gated(*args, **kwargs):
        with semaphore:
            return score(*args, **kwargs)
    packed.perceptual_score = gated


def _measure(task):
    from tools import packed_sprite_assets as packed
    key, group, ffmpeg, points = task
    return key, packed.measure_points(key.split("/", 1)[1], group, ffmpeg, points)


def chunks(points, count):
    """Split a group's points into `count` similar chunks, dealt round-robin so the
    slow ones (high quality, video) are spread across them."""
    count = max(1, min(count, len(points)))
    return [points[i::count] for i in range(count)]


def rounded(entry):
    """Stable output: scores to two decimals, sizes as integers."""
    def point(values):
        return None if values is None else [int(values[0]), int(values[1])] + (
            [round(float(values[2]), 2)] if len(values) > 2 else [])
    return dict(entry, lossless=point(entry["lossless"]),
                webp={k: point(v) for k, v in sorted(entry["webp"].items(), key=lambda kv: float(kv[0]))},
                h264={k: point(v) for k, v in sorted(entry["h264"].items(), key=lambda kv: float(kv[0]))})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=None, help="Curve file (default: tools/sprite-quality-curves.json)")
    parser.add_argument("--only", action="append", default=[], help="Measure only groups matching this glob")
    parser.add_argument("--jobs", type=int,
                        help="Concurrent groups (default: a third of the CPUs; each holds a whole group's "
                             "frames and decoded candidates, and HD video groups need several GB)")
    parser.add_argument("--scorers", type=int, default=4,
                        help="Workers allowed to score at once (default 4; libjxl peaks near 0.8 GB on HD groups)")
    parser.add_argument("--check", action="store_true", help="List stale or missing groups and exit 1 if any")
    args = parser.parse_args()

    if not images.encoder_ready(True, True):
        # Measuring needs the pinned encoder and brotli for transfer sizes (scores come from libjxl).
        python = images.encoder_python(True, True)
        os.execv(python, [python, str(Path(__file__).resolve()), *sys.argv[1:]])

    from tools import packed_sprite_assets as packed
    if not args.check:
        installed = packed.libjxl_version()
        if installed != packed.LIBJXL_VERSION or shutil.which("ssimulacra2") is None:
            sys.exit(f"Curves are scored with libjxl {packed.LIBJXL_VERSION}'s ssimulacra2 tool; found "
                     f"{installed or 'none'} (Homebrew: brew install jpeg-xl; Debian/Ubuntu: libjxl-tools "
                     "with devtools). Update LIBJXL_VERSION in tools/packed_sprite_assets.py to move "
                     "every curve to another version.")
    output = args.output or packed.CURVE_FILE
    try:
        existing = json.loads(output.read_text()).get("groups", {})
    except (OSError, ValueError):
        existing = {}
    groups = sprite_groups()
    selected = {key: group for key, group in groups.items()
                if not args.only or any(fnmatch.fnmatch(key, pattern) for pattern in args.only)}
    stale = sorted(key for key, group in selected.items() if not current(existing.get(key), group))
    removed = sorted(set(existing) - set(groups))
    if args.check:
        for key in stale:
            print("stale " + key)
        for key in removed:
            print("obsolete " + key)
        return 1 if stale or removed else 0

    jobs = args.jobs or max(1, (os.cpu_count() or 3) // 3)
    ffmpeg = sprite_ffmpeg(jobs) if any(selected[key]["crf"] is not None for key in stale) else None
    print(f"Measuring {len(stale)} of {len(selected)} selected sprite groups "
          f"({len(selected) - len(stale)} current, {len(groups)} in all)", file=sys.stderr)
    measured = dict(existing)
    for key in removed:
        measured.pop(key)

    def write():
        document = dict(version=packed.CURVE_VERSION, webp_grid=list(packed.WEBP_GRID),
                        crf_grid=[float(c) for c in packed.CRF_GRID],
                        groups={key: rounded(measured[key]) for key in sorted(measured)})
        temp = output.with_suffix(".tmp")
        # One line per curve point keeps the file short and its diffs readable.
        text = re.sub(r"\[\s+([-0-9.,\s]*?|null)\s+\]",
                      lambda match: "[" + ", ".join(v.strip() for v in match.group(1).split(",")) + "]",
                      json.dumps(document, indent=1, sort_keys=True))
        temp.write_text(text + "\n")
        os.replace(temp, output)

    # Alternate HD and native groups, video groups first within each, so heavy HD groups
    # never fill every worker at once; write as groups finish so an interrupted run
    # keeps its progress. When only a few groups are stale, each is split into chunks of
    # its points so every worker has work: one changed asset takes about a minute, not
    # one core for several.
    def slowest(keys):
        return sorted(keys, key=lambda key: (selected[key]["crf"] is None, key))
    hd = slowest(key for key in stale if key.startswith("hd/"))
    native = slowest(key for key in stale if not key.startswith("hd/"))
    order = [key for pair in zip(hd, native) for key in pair]
    order += hd[len(native):] + native[len(hd):]
    import multiprocessing
    semaphore = multiprocessing.get_context("spawn").Semaphore(max(1, args.scorers))
    with ProcessPoolExecutor(max_workers=jobs, mp_context=multiprocessing.get_context("spawn"),
                             initializer=_gate_scoring, initargs=(semaphore,)) as pool:
        per_group = max(1, -(-2 * jobs // max(1, len(order))))
        partial, waiting, futures = {}, {}, []
        for key in order:
            parts = chunks(packed.curve_points(selected[key]), per_group)
            partial[key], waiting[key] = packed.curve_header(selected[key]) | dict(webp={}, h264={}), len(parts)
            futures += [pool.submit(_measure, (key, selected[key], ffmpeg, part)) for part in parts]
        done = 0
        for future in as_completed(futures):
            key, points = future.result()
            entry = partial[key]
            entry["webp"].update(points.pop("webp"))
            entry["h264"].update(points.pop("h264"))
            entry.update(points)
            waiting[key] -= 1
            if waiting[key] == 0:
                measured[key] = entry
                write()
                done += 1
                print(f"[{done}/{len(order)}] {key}", file=sys.stderr, flush=True)
    write()
    return 0


if __name__ == "__main__":
    sys.exit(main())
