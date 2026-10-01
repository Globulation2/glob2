#!/usr/bin/env python3
"""Count changed pixels inside the menu's paper panel, ignoring the animated colony behind it."""
import sys
from collections import Counter
from pathlib import Path
from PIL import Image, ImageChops

def panel_box(img):
    w, h = img.size
    centre = img.crop((w // 2 - 40, h // 2 - 40, w // 2 + 40, h // 2 + 40))
    paper = Counter(centre.getdata()).most_common(1)[0][0]
    mask = img.point(lambda v: 0)  # placeholder, replaced below
    px = img.load()
    xs, ys = [], []
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            p = px[x, y]
            if all(abs(p[i] - paper[i]) <= 3 for i in range(3)):
                xs.append(x); ys.append(y)
    if len(xs) < 0.05 * w * h / 4:
        return None
    return min(xs), min(ys), max(xs) + 2, max(ys) + 2

def compare(before, after):
    rows = []
    for img in sorted(before.glob("*.bmp")):
        if img.stem.startswith(("game-", "editor-", "gesture-", "replay-")):
            continue
        other = after / img.name
        if not other.exists():
            continue
        a, b = Image.open(img).convert("RGB"), Image.open(other).convert("RGB")
        if a.size != b.size:
            rows.append((img.stem, "size changed")); continue
        box = panel_box(a)
        if not box:
            rows.append((img.stem, "no panel")); continue
        d = ImageChops.difference(a.crop(box), b.crop(box)).convert("L").point(lambda v: 255 if v > 0 else 0)
        rows.append((img.stem, d.histogram()[255]))
    return rows

if __name__ == "__main__":
    for b, a in zip(sys.argv[1::2], sys.argv[2::2]):
        rows = compare(Path(b), Path(a))
        same = [n for n, v in rows if v == 0]
        print(f"## {b} -> {a}: {len(same)}/{len(rows)} menu panels identical")
        for n, v in rows:
            if v != 0:
                print(f"   {n}: {v}")
