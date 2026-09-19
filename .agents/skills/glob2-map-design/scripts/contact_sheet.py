#!/usr/bin/env python3
"""Render a labelled grid of map previews.

  python3 contact_sheet.py OUT.png --binary B --generator tug --cell 256 --cols 4 \
      --panel "seed=1" --panel "seed=2:march=40" ...

Each --panel is "LABEL" or "LABEL:k=v,k=v"; seed=N inside the settings picks the seed.
"""
import argparse, os, re, subprocess, tempfile
from PIL import Image, ImageDraw, ImageFont


def font(size):
    for path in ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
                 '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def render(binary, generator, seed, w, h, teams, settings, cell):
    fd, png = tempfile.mkstemp(suffix='.png')
    os.close(fd)
    cmd = [binary, '--generate-map', generator, '--seed', str(seed), '--width', str(w),
           '--height', str(h), '--teams', str(teams), '--preview', png,
           '--preview-size', str(cell)]
    for k, v in settings.items():
        cmd += ['--set', f'{k}={v}']
    p = subprocess.run(cmd, capture_output=True, text=True)
    ok = '[complete]' in (p.stdout + p.stderr)
    img = Image.open(png).convert('RGB') if ok and os.path.getsize(png) else None
    if img:
        img = img.copy()
    os.unlink(png)
    return img, ok, (p.stdout + p.stderr).strip().splitlines()[-1] if not ok else ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--generator', required=True)
    ap.add_argument('--cell', type=int, default=256)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--width', type=int, default=256)
    ap.add_argument('--height', type=int, default=256)
    ap.add_argument('--teams', type=int, default=4)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--panel', action='append', required=True)
    ap.add_argument('--title', default='')
    a = ap.parse_args()

    label_h, pad, title_h = 22, 6, (30 if a.title else 0)
    panels = []
    for spec in a.panel:
        label, _, kv = spec.partition(':')
        settings, seed, w, h, teams = {}, a.seed, a.width, a.height, a.teams
        for part in filter(None, kv.split(',')):
            k, v = part.split('=')
            if k == 'seed':
                seed = int(v)
            elif k == 'width':
                w = int(v)
            elif k == 'height':
                h = int(v)
            elif k == 'teams':
                teams = int(v)
            else:
                settings[k] = v
        img, ok, err = render(a.binary, a.generator, seed, w, h, teams, settings, a.cell)
        panels.append((label, img, ok, err))
        print(f'{label:34s} {"ok" if ok else "REFUSED: " + err}', flush=True)

    cols = min(a.cols, len(panels))
    rows = (len(panels) + cols - 1) // cols
    cw, ch = a.cell + pad, a.cell + label_h + pad
    sheet = Image.new('RGB', (cols * cw + pad, rows * ch + pad + title_h), (24, 24, 28))
    draw = ImageDraw.Draw(sheet)
    if a.title:
        draw.text((pad + 2, 7), a.title, font=font(17), fill=(255, 255, 255))
    for i, (label, img, ok, err) in enumerate(panels):
        x, y = pad + (i % cols) * cw, pad + title_h + (i // cols) * ch
        if img:
            img = img.resize((a.cell, a.cell), Image.LANCZOS)
            sheet.paste(img, (x, y))
        else:
            draw.rectangle([x, y, x + a.cell, y + a.cell], fill=(70, 30, 30))
            draw.text((x + 8, y + a.cell // 2), 'refused', font=font(14), fill=(255, 180, 180))
        draw.text((x + 2, y + a.cell + 4), label, font=font(13),
                  fill=(255, 255, 255) if ok else (255, 150, 150))
    sheet.save(a.out)
    print('wrote', a.out, sheet.size)


if __name__ == '__main__':
    main()
