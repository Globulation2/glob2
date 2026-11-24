#!/usr/bin/env python3
"""Regenerate checked-in application icons from Glob2's high-resolution icon master.

The master is content-src/icons/app/glob2-icon-1024.png, rendered from the vector
glob2-icon.svg beside it (--render-master, which needs resvg-py). Requires Pillow
only when regenerating assets, not when building any app. Keep the artwork
unchanged: resize it onto an opaque in-game purple background, or a padded
transparent layer for Android's launcher-controlled adaptive mask. Desktop sizes
up to 128 pixels keep their own files in content/data/icons, drawn for those sizes.
"""
import argparse
import io
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
VECTOR = ROOT / "content-src/icons/app/glob2-icon.svg"
SOURCE = ROOT / "content-src/icons/app/glob2-icon-1024.png"
DESKTOP = ROOT / "content/data/icons"
BACKGROUND = (43, 28, 66, 255)
IOS_BACKGROUND = (76, 54, 118, 255)


def resized(source, side):
    # Premultiplied, so transparent pixels cannot darken the outline.
    return source.convert("RGBa").resize((side, side), Image.Resampling.LANCZOS).convert("RGBA")


def render(source, path, size, fraction, transparent=False, background=BACKGROUND):
    side = round(size * fraction)
    artwork = resized(source, side)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0) if transparent else background)
    canvas.alpha_composite(artwork, ((size - side) // 2, (size - side) // 2))
    path.parent.mkdir(parents=True, exist_ok=True)
    (canvas if transparent else canvas.convert("RGB")).save(path)


def render_master():
    import resvg_py
    png = resvg_py.svg_to_bytes(svg_string=VECTOR.read_text(), width=1024, height=1024)
    Image.open(io.BytesIO(bytes(png))).convert("RGBA").save(SOURCE, optimize=True)


def desktop(source):
    """Transparent desktop icons: large sizes from the master, small ones as drawn."""
    for size in (256, 512):
        resized(source, size).save(DESKTOP / f"glob2-icon-{size}x{size}.png", optimize=True)
    images = {size: Image.open(DESKTOP / f"glob2-icon-{size}x{size}.png").convert("RGBA")
              for size in (16, 24, 32, 48, 64, 128, 256, 512)}
    images[1024] = source
    images[512].save(ROOT / "fastlane/metadata/android/en-US/images/icon.png", optimize=True)
    images[256].save(ROOT / "platforms/windows/glob2.ico", format="ICO", bitmap_format="bmp",
                     sizes=[(size, size) for size in (16, 24, 32, 48, 64, 128, 256)],
                     append_images=[images[size] for size in (16, 24, 32, 48, 64, 128)])
    icns = ROOT / "platforms/macos/Glob2.icns"
    names = {16: ("16x16",), 32: ("16x16@2x", "32x32"), 64: ("32x32@2x",), 128: ("128x128",),
             256: ("128x128@2x", "256x256"), 512: ("256x256@2x", "512x512"), 1024: ("512x512@2x",)}
    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as directory:
            iconset = Path(directory) / "Glob2.iconset"
            iconset.mkdir()
            for size, labels in names.items():
                for label in labels:
                    images[size].save(iconset / f"icon_{label}.png")
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
    else:
        # Pillow omits the 16 and 32 pixel entries; regenerate on macOS before committing.
        images[1024].save(icns, format="ICNS", append_images=[images[size] for size in names if size != 1024])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--render-master", action="store_true",
                        help="first re-render the PNG master from the vector (needs resvg-py)")
    if parser.parse_args().render_master:
        render_master()
    source = Image.open(SOURCE).convert("RGBA")
    desktop(source)
    res = ROOT / "platforms/mobile/android/app/src/main/res"
    for density, scale in (("mdpi", 1), ("hdpi", 1.5), ("xhdpi", 2),
                           ("xxhdpi", 3), ("xxxhdpi", 4)):
        folder = res / ("mipmap-" + density)
        render(source, folder / "ic_launcher.png", round(48 * scale), .82)
        # The outer adaptive layer is reserved for launcher masks and motion.
        render(source, folder / "ic_launcher_foreground.png", round(108 * scale), .5, True)

    catalog = ROOT / "platforms/mobile/ios/Assets.xcassets"
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
                render(source, appicon / filename, pixels, .82,
                       background=IOS_BACKGROUND)
                entries.append({"idiom": idiom, "size": f"{size}x{size}",
                                "scale": f"{scale}x", "filename": filename})
    (appicon / "Contents.json").write_text(json.dumps({"images": entries, "info": info}, indent=2) + "\n")


if __name__ == "__main__":
    main()
