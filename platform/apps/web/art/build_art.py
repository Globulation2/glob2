#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the web app's artwork from the game's own data (GPL-3.0, see
docs/assets/source-attribution.md and platform/apps/web/art/README.md).

    python3 platform/apps/web/art/build_art.py

Writes compressed WebP/PNG files to platform/apps/web/src/art/ (imported by
the app, so Vite fingerprints them) and the few the server-rendered sign-in
and invite pages need to platform/apps/api/src/web/static/. Needs Pillow.
The outputs are committed; rerun this after changing a source sprite.
"""
from __future__ import annotations

import colorsys
import os
import shutil
from pathlib import Path

from PIL import Image, ImageFilter

REPO = Path(__file__).resolve().parents[4]
GFX = REPO / "data/gfx"
HIGHRES = REPO / "data/highres/v1"
OUT = REPO / "platform/apps/web/src/art"
STATIC = REPO / "platform/apps/api/src/web/static"
PUBLIC = REPO / "platform/apps/web/public"

# Unit sprite atlas layout (src/unit/render/UnitSkin.cpp, src/unit/render/UnitAnimation.h):
# frame = actionBase * 4 + direction * 32 + pose; direction 3 moves east, 7 west.
WORKER_WALK = 64 * 4
EXPLORER_FLY = 0
WARRIOR_WALK = 256 * 4
EAST, WEST = 3, 7


def sprite(name: str) -> Image.Image:
    for base in (HIGHRES, GFX):
        path = base / f"{name}.png"
        if path.exists():
            return Image.open(path).convert("RGBA")
    raise FileNotFoundError(name)


def unit_frame(index: int) -> Image.Image:
    """Shadow layer (unitN.png, when present) under the team-coloured body (unitNr.png)."""
    body = sprite(f"unit{index}r")
    out = Image.new("RGBA", body.size)
    shadow = HIGHRES / f"unit{index}.png"
    if shadow.exists():
        out.alpha_composite(Image.open(shadow).convert("RGBA"))
    out.alpha_composite(body)
    return out


def save_webp(image: Image.Image, name: str, quality: int = 80, lossless: bool = False) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    image.save(path, "WEBP", quality=quality, method=6, lossless=lossless)
    return path


def strip(base: int, direction: int, size: int, step: int = 2) -> Image.Image:
    """One walk/fly cycle as a horizontal strip (every `step`-th of the 32 poses)."""
    poses = list(range(0, 32, step))
    out = Image.new("RGBA", (size * len(poses), size))
    for i, pose in enumerate(poses):
        frame = unit_frame(base + direction * 32 + pose).resize((size, size), Image.LANCZOS)
        out.alpha_composite(frame, (i * size, 0))
    return out


def wordmark_masks() -> tuple[Image.Image, Image.Image]:
    """Alpha masks of the menu wordmark: the green letters and the gold "2"."""
    src = Image.open(GFX / "menu-wordmark.png").convert("RGB")
    bg = src.getpixel((4, 4))
    w, h = src.size
    letters = Image.new("L", (w, h))
    two = Image.new("L", (w, h))
    lp, tp = letters.load(), two.load()
    px = src.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            dist = max(abs(r - bg[0]), abs(g - bg[1]), abs(b - bg[2]))
            alpha = max(0, min(255, int((dist - 12) * 255 / 150)))
            if alpha == 0:
                continue
            hue, sat, _ = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if 0.06 < hue < 0.16 and sat > 0.25:
                tp[x, y] = alpha
            else:
                lp[x, y] = alpha
    box = letters.getbbox()
    two_box = two.getbbox()
    left = min(box[0], two_box[0]) - 8
    top = min(box[1], two_box[1]) - 8
    right = max(box[2], two_box[2]) + 8
    bottom = max(box[3], two_box[3]) + 8
    size = (900, round(900 * (bottom - top) / (right - left)))

    def finish(mask: Image.Image) -> Image.Image:
        cut = mask.crop((left, top, right, bottom)).resize(size, Image.LANCZOS)
        rgba = Image.new("RGBA", size, (255, 255, 255, 0))
        rgba.putalpha(cut)
        return rgba

    return finish(letters), finish(two)


def building(name: str, size: int = 96) -> Image.Image:
    """A building with its team layer, the way the game composes them."""
    image = sprite(name)
    team = None
    for base in (HIGHRES, GFX):
        if (base / f"{name}r.png").exists():
            team = Image.open(base / f"{name}r.png").convert("RGBA").resize(image.size)
            break
    if team is not None:
        image.alpha_composite(team)
    return fit(image, size)


def fit(image: Image.Image, size: int) -> Image.Image:
    image = image.crop(image.getbbox())
    w, h = image.size
    scale = size / max(w, h)
    image = image.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (size, size))
    out.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))
    return out


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    STATIC.mkdir(parents=True, exist_ok=True)

    # Hero: the live menu colony (data/gfx/menu-colony.png, the game's own menu backdrop).
    colony = Image.open(GFX / "menu-colony.png").convert("RGB")
    save_webp(colony, "colony-1600.webp", quality=60)
    save_webp(colony.resize((960, 540), Image.LANCZOS), "colony-960.webp", quality=62)
    # A soft, small copy for the server-rendered pages' backdrop.
    soft = colony.resize((960, 540), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.6))
    soft.save(STATIC / "colony.webp", "WEBP", quality=55, method=6)
    # Link previews (OpenGraph) of invite pages: 1200x630, the size chat apps expect.
    og = colony.crop((200, 130, 1400, 760))
    og.save(STATIC / "og-colony.jpg", "JPEG", quality=70, optimize=True, progressive=True)

    letters, two = wordmark_masks()
    save_webp(letters, "wordmark-letters.webp", lossless=True)
    save_webp(two, "wordmark-two.webp", lossless=True)
    letters.save(STATIC / "wordmark-letters.webp", "WEBP", lossless=True, method=6)
    two.save(STATIC / "wordmark-two.webp", "WEBP", lossless=True, method=6)

    # Globs: walk and fly cycles at 2x the game's 32 px for sharp high-DPI screens.
    save_webp(strip(WORKER_WALK, EAST, 64), "worker-east.webp", quality=82)
    save_webp(strip(WORKER_WALK, WEST, 64), "worker-west.webp", quality=82)
    save_webp(strip(WARRIOR_WALK, EAST, 64), "warrior-east.webp", quality=82)
    save_webp(strip(EXPLORER_FLY, WEST, 64), "explorer-west.webp", quality=82)

    # Buildings, flags and resources used as page icons and decoration.
    for name, label, size in (
        ("swarm0b0", "swarm", 160),
        ("inn0b0", "inn", 96),
        ("school1b0", "school", 96),
        ("racetrack0b0", "racetrack", 96),
        ("pool0b0", "pool", 96),
        ("explorationflag0", "exploration-flag", 96),
        ("warflag0", "war-flag", 96),
        ("clearingflag0", "clearing-flag", 96),
        ("hosp0b0", "hospital", 96),
    ):
        save_webp(building(name, size), f"{label}.webp", quality=82)
    for name, label in (
        ("ressource4", "wood"),
        ("ressource14", "fruit"),
        ("ressource24", "papyrus"),
        ("ressource38", "stone"),
        ("ressource44", "algae"),
    ):
        save_webp(fit(sprite(name), 96), f"{label}.webp", quality=82)

    # Favicons from the game icon.
    PUBLIC.mkdir(parents=True, exist_ok=True)
    shutil.copy(REPO / "data/icons/glob2-icon-32x32.png", PUBLIC / "favicon-32.png")
    shutil.copy(REPO / "mobile/ios/Assets.xcassets/AppIcon.appiconset/icon-180.png",
                PUBLIC / "apple-touch-icon.png")
    shutil.copy(REPO / "data/icons/glob2-icon-32x32.png", STATIC / "favicon-32.png")
    shutil.copy(REPO / "data/icons/glob2-icon-64x64.png", OUT / "glob-64.png")
    shutil.copy(REPO / "data/icons/glob2-icon-64x64.png", STATIC / "glob-64.png")

    # Fonts for the server-rendered pages: the game font and the body font.
    shutil.copy(PUBLIC / "fonts/glob2-sans.woff2", STATIC / "glob2-sans.woff2")
    nunito = REPO / "platform/node_modules/@fontsource-variable/nunito/files/nunito-latin-wght-normal.woff2"
    if nunito.exists():
        shutil.copy(nunito, STATIC / "nunito.woff2")
        shutil.copy(nunito.parents[1] / "LICENSE", STATIC / "LICENSE-Nunito.txt")
        shutil.copy(nunito.parents[1] / "LICENSE", PUBLIC / "fonts/LICENSE-Nunito.txt")
    else:
        print("note: run npm ci in platform/ to copy the Nunito font")

    for path in sorted(list(OUT.iterdir()) + list(STATIC.iterdir())):
        print(f"{os.path.getsize(path):>8}  {path.relative_to(REPO)}")


if __name__ == "__main__":
    main()
