#!/usr/bin/env python3
"""Inspect Glob2 chapter manifests and extract footage with FFmpeg (no shell)."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def load_recording(source: Path) -> tuple[Path, dict]:
    manifest_path = source if source.suffix == ".json" else Path(str(source) + ".json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported recording manifest version")
    name = manifest.get("video", "")
    if not isinstance(name, str) or not name or Path(name).name != name or "/" in name or "\\" in name:
        raise ValueError("Manifest video must be a filename beside the manifest")
    video = manifest_path.parent / name
    chapters = manifest.get("chapters")
    if not isinstance(chapters, list):
        raise ValueError("Manifest chapters must be a list")
    last_end = 0
    ids = set()
    for chapter in chapters:
        if not isinstance(chapter, dict):
            raise ValueError("Chapter must be an object")
        start, end = chapter["start_us"], chapter["end_us"]
        if type(start) is not int or type(end) is not int or start < last_end or end <= start:
            raise ValueError("Invalid or overlapping chapter intervals")
        if type(chapter["id"]) is not int or chapter["id"] < 1:
            raise ValueError("Chapter IDs must be positive integers")
        if not isinstance(chapter.get("title"), str) or not isinstance(chapter.get("screen"), str):
            raise ValueError("Chapter title and screen must be strings")
        if chapter["id"] in ids:
            raise ValueError("Duplicate chapter ID")
        ids.add(chapter["id"])
        last_end = end
    duration = manifest.get("duration_us")
    if duration is not None and (type(duration) is not int or duration < last_end):
        raise ValueError("Chapter exceeds recording duration")
    return video, manifest


def select_chapters(manifest: dict, args: argparse.Namespace) -> list[dict]:
    selected = []
    for chapter in manifest["chapters"]:
        tests = [("id", args.chapter), ("screen", args.screen), ("dialog", args.dialog),
                 ("match", args.match), ("phase", args.phase), ("era_start", args.era_start)]
        if all(not values or chapter.get(key) in values for key, values in tests):
            selected.append(chapter)
    return selected


def command(video: Path, output: Path, chapter: dict, args: argparse.Namespace) -> list[str]:
    start = f'{chapter["start_us"] / 1_000_000:.6f}'
    duration = f'{(chapter["end_us"] - chapter["start_us"]) / 1_000_000:.6f}'
    cmd = [args.ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin", "-n"]
    if args.copy:
        cmd += ["-noaccurate_seek"]
    cmd += ["-ss", start, "-i", str(video), "-t", duration, "-map", "0:v:0", "-map", "0:a:0?",
            "-map_metadata", "-1", "-map_chapters", "-1", "-metadata", "title=" + chapter["title"]]
    if args.copy:
        cmd += ["-c", "copy", "-avoid_negative_ts", "make_zero"]
    elif args.format == "webm":
        cmd += ["-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-row-mt", "1", "-c:a", "libopus", "-b:a", "128k"]
    else:
        cmd += ["-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k"]
    if args.format == "mp4":
        cmd += ["-movflags", "+faststart"]
    return cmd + [str(output)]


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    sub = root.add_subparsers(dest="action", required=True)
    for name in ("list", "extract"):
        p = sub.add_parser(name)
        p.add_argument("recording", type=Path, help="MP4 or its .mp4.json manifest")
        p.add_argument("--chapter", type=int, action="append")
        p.add_argument("--screen", action="append")
        p.add_argument("--dialog", action="append")
        p.add_argument("--match", type=int, action="append")
        p.add_argument("--phase", action="append", choices=["menu", "loading", "gameplay", "dialog", "results"])
        p.add_argument("--era-start", type=int, action="append", help="Absolute tick bucket start, e.g. 10000")
        if name == "list":
            p.add_argument("--json", action="store_true")
        else:
            p.add_argument("--output-dir", type=Path, required=True)
            p.add_argument("--format", choices=["mp4", "webm"], default="mp4")
            p.add_argument("--copy", action="store_true", help="Approximate keyframe-aligned MP4 cuts, without re-encoding")
            p.add_argument("--ffmpeg", default="ffmpeg")
            p.add_argument("--dry-run", action="store_true")
    return root


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        video, manifest = load_recording(args.recording)
        chapters = select_chapters(manifest, args)
        if args.action == "list":
            if args.json:
                print(json.dumps(chapters, ensure_ascii=False, indent=2))
            else:
                for chapter in chapters:
                    print(f'{chapter["id"]:4}  {chapter["start_us"]/1e6:10.3f}–{chapter["end_us"]/1e6:10.3f}s  {chapter["title"]}')
            return 0
        if not manifest.get("complete") or not video.is_file():
            raise ValueError("Recording is incomplete; inspect the retained .recording directory")
        if not chapters:
            raise ValueError("No chapters matched the selection")
        if args.copy and args.format != "mp4":
            raise ValueError("Stream-copy mode supports MP4 only")
        if args.copy:
            print("Stream-copy cuts are approximate; use the default mode for exact boundaries.", file=sys.stderr)
        outputs = []
        for chapter in chapters:
            label = re.sub(r"[^a-zA-Z0-9_-]", "-", chapter.get("dialog") or chapter["screen"])
            output = args.output_dir / f'{video.stem}-chapter-{chapter["id"]:04d}-{label}.{args.format}'
            if output.exists() or Path(str(output) + ".json").exists():
                raise ValueError(f"Output already exists: {output}")
            outputs.append((chapter, output))
        if not args.dry_run:
            args.output_dir.mkdir(parents=True, exist_ok=True)
        for chapter, output in outputs:
            cmd = command(video, output, chapter, args)
            if args.dry_run:
                print(json.dumps(cmd, ensure_ascii=False))
                continue
            subprocess.run(cmd, check=True)
            metadata = {"version": 1, "video": output.name, "source": video.name,
                        "source_chapter": chapter, "approximate": args.copy}
            # Exclusive create protects a sidecar produced by another extraction process.
            with Path(str(output) + ".json").open("x", encoding="utf-8") as file:
                json.dump(metadata, file, ensure_ascii=False, indent=2)
                file.write("\n")
            print(output)
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Recording tool: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
