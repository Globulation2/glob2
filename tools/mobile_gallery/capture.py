#!/usr/bin/env python3
"""Capture production mobile screens and assemble a portable, offline review gallery.

Run from any directory. Each size gets a fresh profile; no personal saves or
preferences are written. Images, logs, provenance and feedback stay in artifacts/.
The SDL dummy video driver avoids macOS window-size clamping, while SDL's software
renderer captures the final viewport (including real legacy letterboxing).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
from datetime import datetime, timezone

from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[2]
ASSETS = Path(__file__).resolve().parent
SIZES = [
    ("small-portrait", "Small phone · portrait", 320, 568),
    ("small-landscape", "Small phone · landscape", 568, 320),
    ("phone-portrait", "Phone · portrait", 390, 844),
    ("phone-landscape", "Phone · landscape", 844, 390),
    ("tablet-portrait", "Tablet · portrait", 768, 1024),
    ("tablet-landscape", "Tablet · landscape", 1024, 768),
    ("tablet-automatic", "Tablet · portrait · Automatic touch", 768, 1024),
    ("tablet-spacious", "Tablet · landscape · Spacious touch", 1024, 768),
    ("desktop-laptop", "Desktop · laptop", 1280, 800),
    ("desktop-fullhd", "Desktop · Full HD", 1920, 1080),
]


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor-only", action="store_true", help="Capture only map and campaign editor screens")
    parser.add_argument("--game-only", action="store_true", help="Capture only gameplay and replay screens")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/darwin/client/release/src/mobile-gallery")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/mobile-gallery/review")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--extend", action="store_true", help="Capture missing sizes while preserving verified existing images and their provenance")
    modes.add_argument("--reuse", action="store_true", help="Rebuild HTML from existing raw captures without running the game")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    original = None
    if args.extend or args.reuse:
        manifest_path = output / "manifest.json"
        if not manifest_path.exists():
            parser.error("--extend and --reuse require an existing capture manifest")
        original = json.loads(manifest_path.read_text())
    catalog = json.loads((ASSETS / "catalog.json").read_text())
    if args.editor_only:
        catalog["screens"] = [entry for entry in catalog["screens"] if entry["group"] == "Map & campaign editor"]
    if args.game_only:
        catalog["screens"] = [entry for entry in catalog["screens"] if entry["group"] == "Gameplay"]
    manifest = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "commit": git("rev-parse", "HEAD"),
        "dirty": bool(git("status", "--porcelain")),
        "renderer": "Native shared UI · SDL software renderer · Compact phones/tablets + Spacious touch tablet + desktop mouse presentation · English · 100% UI scale",
        "sizes": [dict(id=id, label=label, width=w, height=h, presentation="desktop" if id.startswith("desktop-") else "touch-spacious" if id=="tablet-spacious" else "touch-auto" if id=="tablet-automatic" else "compact") for id, label, w, h in SIZES],
        "screens": [], "gaps": catalog["gaps"],
    }
    binary = args.binary.resolve()
    if binary.exists():
        manifest["binarySha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    # Provenance belongs to each size: extensions can use a newer harness without
    # falsely attributing existing mobile screenshots to the new executable.
    provenance_fields = ("generated", "commit", "dirty", "binarySha256")
    current_provenance = {field: manifest[field] for field in provenance_fields if field in manifest}
    manifest["captureProvenance"] = {}
    if original:
        for size in original["sizes"]:
            manifest["captureProvenance"][size["id"]] = original.get("captureProvenance", {}).get(
                size["id"], {field: original[field] for field in provenance_fields if field in original})
        if args.reuse:
            for field in provenance_fields:
                if field in original:
                    manifest[field] = original[field]
    if not args.reuse:
        source_dir = output / "capture-sources" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        source_dir.mkdir(parents=True)
        (source_dir / "source.patch").write_text(git("diff", "--binary"))
        # A diff omits newly introduced components until they are staged. Preserve
        # those source files as well so a dirty-tree capture is reproducible.
        for name in git("ls-files", "--others", "--exclude-standard").splitlines():
            source = ROOT / name
            if source.is_file():
                target = source_dir / "untracked" / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
        shutil.copytree(ASSETS, source_dir / "tool", ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copyfile(ROOT / "tools/MobileGalleryHarness.cpp", source_dir / "MobileGalleryHarness.cpp")
        current_provenance["sourceDirectory"] = source_dir.relative_to(output).as_posix()
        # Keep the original top-level link for existing galleries.
        if not (output / "source.patch").exists():
            shutil.copyfile(source_dir / "source.patch", output / "source.patch")
    previous_screens = {screen["id"]: screen for screen in original["screens"]} if original else {}
    previous_sizes = {size["id"] for size in original["sizes"]} if original else set()
    known = {entry["id"] for entry in catalog["screens"]}
    def available(entry, size_id):
        scope = entry.get("availability", "all")
        return (
            scope != "retired"
            and (scope != "mobile" or not size_id.startswith("desktop-"))
            and (scope != "desktop" or size_id.startswith("desktop-"))
            and (scope != "phone" or size_id.startswith(("small-", "phone-")))
        )
    captures: dict[str, dict] = {id: {} for id in known}
    for id, label, width, height in SIZES:
        profile = output / "raw" / id
        presentation = "desktop" if id.startswith("desktop-") else "touch-spacious" if id=="tablet-spacious" else "touch-auto" if id=="tablet-automatic" else "compact"
        retaining = args.reuse or (args.extend and id in previous_sizes)
        if not retaining:
            # Never silently mix a new capture with stale images or preferences.
            if profile.exists():
                parser.error(f"Capture profile already exists: {profile}. Choose a new --output or use --reuse.")
            profile.mkdir(parents=True)
            env = os.environ.copy()
            if args.editor_only:
                env["GLOB2_GALLERY_EDITOR_ONLY"] = "1"
            if args.game_only:
                env["GLOB2_GALLERY_GAME_ONLY"] = "1"
            env.update(GLOB2_USER_DATA_DIR=str(profile), SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
            print(f"Capturing {label} ({width} × {height})", flush=True)
            with (output / f"{id}.log").open("w") as log:
                subprocess.run([str(binary), str(width), str(height), presentation], cwd=ROOT, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
            missing_strings = sorted(set(re.findall(r'StringTable::getString\("(\[[^\n]*?\])', (output / f"{id}.log").read_text(errors="replace"))))
            if missing_strings:
                raise RuntimeError(f"Missing translation keys for {id}: {', '.join(missing_strings)}")
            manifest["captureProvenance"][id] = dict(current_provenance)
            fixture = re.search(r"FIXTURE_CHECKSUM (\d+)", (output / f"{id}.log").read_text())
            if fixture:
                manifest["captureProvenance"][id]["fixtureChecksum"] = fixture.group(1)
        for prefix, label, field in (("build", "building-drag", "recordings"),
                                     ("zone", "zone-stroke", "strokeRecordings"),
                                     ("editor-build", "editor-building-drag", "editorRecordings"),
                                     ("editor-paint", "editor-paint-stroke", "editorStrokeRecordings")):
            gesture_frames = sorted(profile.glob(f"gesture-{prefix}-*.bmp"))
            if gesture_frames:
                animation = output / "recordings" / f"{id}-{label}.gif"
                animation.parent.mkdir(exist_ok=True)
                frames = [Image.open(path).convert("RGB") for path in gesture_frames]
                frames[0].save(animation, save_all=True, append_images=frames[1:], duration=[500]+[90]*(len(frames)-2)+[1000], loop=0)
                manifest.setdefault(field, {})[id] = animation.relative_to(output).as_posix()
        images = sorted(path for path in profile.glob("*.bmp") if not path.stem.startswith("gesture-"))
        if not images:
            raise RuntimeError(f"No captures found for {id}")
        for path in images:
            if path.stem not in known:
                raise RuntimeError(f"Undocumented capture: {path.stem}; add it to catalog.json")
            with Image.open(path) as source:
                image = source.convert("RGB")
            if image.size != (width, height):
                raise RuntimeError(f"Wrong output size: {path}: {image.size}, expected {(width, height)}")
            if max(ImageStat.Stat(image).stddev) < 1:
                raise RuntimeError(f"Blank capture: {path}")
            relative = Path("images") / id / f"{path.stem}.png"
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            previous = previous_screens.get(path.stem, {}).get("captures", {}).get(id)
            if retaining and previous and target.exists():
                # Existing screenshots are immutable during extension. Verify rather
                # than recompressing hundreds of already-reviewed images.
                if hashlib.sha256(target.read_bytes()).hexdigest() != previous["sha256"]:
                    raise RuntimeError(f"Previously captured image changed: {target}")
            else:
                image.save(target, optimize=True)
            captures[path.stem][id] = {"src": relative.as_posix(), "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}
        required = {entry["id"] for entry in catalog["screens"] if available(entry, id)}
        missing = required - {p.stem for p in images}
        if missing:
            raise RuntimeError(f"Missing captures for {id}: {', '.join(sorted(missing))}")
    for entry in catalog["screens"]:
        manifest["screens"].append({**entry, "captures": captures[entry["id"]]})
    for name in ("index.html", "gallery.css", "gallery.js"):
        shutil.copyfile(ASSETS / name, output / name)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # A JS data file makes the gallery work through file:// as well as HTTP.
    (output / "manifest.js").write_text("window.GALLERY = " + json.dumps(manifest).replace("</", "<\\/") + ";\n")
    # Checkpoints share immutable images and feedback identities. They are small
    # review indexes, not new captures with potentially different fixture state.
    foundation = {"main-menu", "main-more", "campaign-menu", "campaign-select", "campaign-saves", "tutorial-missions", "load-game", "load-replay", "credits", "confirmation", "error-message"}
    checkpoints = {
        "foundation": lambda screen: screen["id"] in foundation,
        "settings": lambda screen: screen["id"].startswith("settings-"),
        "setup": lambda screen: screen["id"] != "setup-options" and screen["id"].startswith(("setup-", "landscape-", "ai-", "start-quality")),
    }
    for name, include in checkpoints.items():
        folder = output / "checkpoints" / name
        folder.mkdir(parents=True, exist_ok=True)
        checkpoint = json.loads(json.dumps(manifest))
        checkpoint["screens"] = [screen for screen in checkpoint["screens"] if include(screen)]
        if not checkpoint["screens"]:
            continue
        for screen in checkpoint["screens"]:
            for capture in screen["captures"].values():
                capture["src"] = "../../" + capture["src"]
        for asset in ("index.html", "gallery.css", "gallery.js"):
            shutil.copyfile(ASSETS / asset, folder / asset)
        (folder / "manifest.js").write_text("window.GALLERY = " + json.dumps(checkpoint).replace("</", "<\\/") + ";\n")
        (folder / "manifest.json").write_text(json.dumps(checkpoint, indent=2) + "\n")
    print(f"Gallery: {output / 'index.html'}\n{len(known)} views × {len(SIZES)} sizes", flush=True)


if __name__ == "__main__":
    main()
