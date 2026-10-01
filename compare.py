#!/usr/bin/env python3
"""Before/after pixel diffs and point-normalized comparison sheets for the text-size change."""
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
BEFORE, AFTER, OUT = ROOT / "before", ROOT / "after", ROOT / "compare"
OUT.mkdir(exist_ok=True)
POINTS = {"desktop": (1280, 800), "tablet-landscape": (1024, 768), "phone-portrait": (390, 844),
          "phone-landscape": (844, 390)}


def diff_report():
    lines = []
    for shape in POINTS:
        b, a = BEFORE / shape, AFTER / shape
        same, changed, missing = [], [], []
        for img in sorted(b.glob("*.bmp")):
            other = a / img.name
            if not other.exists():
                missing.append(img.stem)
                continue
            x, y = Image.open(img).convert("RGB"), Image.open(other).convert("RGB")
            if x.size == y.size and ImageChops.difference(x, y).getbbox() is None:
                same.append(img.stem)
            else:
                changed.append(img.stem)
        lines.append(f"## {shape}: {len(same)} identical, {len(changed)} changed, {len(missing)} missing")
        if changed:
            lines.append("changed: " + " ".join(changed))
        if missing:
            lines.append("missing: " + " ".join(missing))
    return "\n".join(lines)


font = ImageFont.load_default(size=18)


def pts(path, shape):
    return Image.open(path).convert("RGB").resize(POINTS[shape], Image.LANCZOS)


def sheet(cells, out, cols):
    W = max(c[1].size[0] for c in cells)
    H = max(c[1].size[1] for c in cells)
    rows = (len(cells) + cols - 1) // cols
    img = Image.new("RGB", (cols * W + (cols + 1) * 10, rows * (H + 34) + 10), (30, 30, 30))
    d = ImageDraw.Draw(img)
    for i, (label, im) in enumerate(cells):
        x, y = 10 + (i % cols) * (W + 10), 10 + (i // cols) * (H + 34)
        d.text((x, y), label, fill=(255, 230, 120), font=font)
        img.paste(im, (x, y + 28))
    img.save(OUT / out)


def pairs(shape, names, out, cols=2, after_dir=None):
    cells = []
    for name in names:
        cells.append((f"BEFORE {shape} {name}", pts(BEFORE / shape / f"{name}.bmp", shape)))
        cells.append((f"AFTER {shape} {name}", pts(AFTER / (after_dir or shape) / f"{name}.bmp", shape)))
    sheet(cells, out, cols)


if __name__ == "__main__":
    report = diff_report()
    (OUT / "diff.txt").write_text(report + "\n")
    print(report)
    for shape in ("phone-landscape", "phone-portrait"):
        cols = 2 if shape == "phone-landscape" else 4
        pairs(shape, ["settings-display", "game-options"], f"{shape}-settings-vs-options.png", cols)
        pairs(shape, ["load-game", "game-load"], f"{shape}-load.png", cols)
        pairs(shape, ["game-map", "game-tutorial"], f"{shape}-hud.png", cols)
    # 150%: menus and in-game side by side, both after.
    for shape in ("phone-landscape", "phone-portrait"):
        d = AFTER / f"{shape}-150"
        names = ["settings-display", "game-options", "setup-map", "game-tutorial"]
        cells = [(f"AFTER 150% {shape} {n}", pts(d / f"{n}.bmp", shape)) for n in names if (d / f"{n}.bmp").exists()]
        sheet(cells, f"{shape}-150.png", 2 if shape == "phone-landscape" else 4)
