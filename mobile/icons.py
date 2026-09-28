#!/usr/bin/env python3
"""Regenerate checked-in launcher assets from Glob2's canonical desktop icon.

Requires Pillow only when regenerating assets, not when building either app.
Keep the artwork unchanged: resize it onto an opaque in-game purple background,
or a padded transparent layer for Android's launcher-controlled adaptive mask.
"""
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data/icons/glob2-icon-128x128.png"
BACKGROUND = (43, 28, 66, 255)


def render(source, path, size, fraction, transparent=False):
    side = round(size * fraction)
    artwork = source.resize((side, side), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0) if transparent else BACKGROUND)
    canvas.alpha_composite(artwork, ((size - side) // 2, (size - side) // 2))
    path.parent.mkdir(parents=True, exist_ok=True)
    (canvas if transparent else canvas.convert("RGB")).save(path)


def main():
    source = Image.open(SOURCE).convert("RGBA")
    res = ROOT / "mobile/android/app/src/main/res"
    for density, scale in (("mdpi", 1), ("hdpi", 1.5), ("xhdpi", 2),
                           ("xxhdpi", 3), ("xxxhdpi", 4)):
        folder = res / ("mipmap-" + density)
        render(source, folder / "ic_launcher.png", round(48 * scale), .82)
        # The outer adaptive layer is reserved for launcher masks and motion.
        render(source, folder / "ic_launcher_foreground.png", round(108 * scale), .5, True)

    catalog = ROOT / "mobile/ios/Assets.xcassets"
    catalog.mkdir(parents=True, exist_ok=True)
    info = {"author": "xcode", "version": 1}
    (catalog / "Contents.json").write_text(json.dumps({"info": info}, indent=2) + "\n")
    appicon = catalog / "AppIcon.appiconset"
    entries = []
    for idiom, sizes, scales in (
        ("iphone", (20, 29, 40, 60), (2, 3)),
        ("ipad", (20, 29, 40, 76), (1, 2)),
        ("ipad", (83.5,), (2,)),
        ("ios-marketing", (1024,), (1,)),
    ):
        for size in sizes:
            for scale in scales:
                pixels = round(size * scale)
                filename = f"icon-{pixels}.png"
                render(source, appicon / filename, pixels, .82)
                entries.append({"idiom": idiom, "size": f"{size}x{size}",
                                "scale": f"{scale}x", "filename": filename})
    (appicon / "Contents.json").write_text(json.dumps({"images": entries, "info": info}, indent=2) + "\n")


if __name__ == "__main__":
    main()
