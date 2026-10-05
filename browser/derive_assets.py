#!/usr/bin/env python3
"""Smaller browser copies of three startup files, checked in under browser/assets/.

The browser downloads the `core` package before the main menu, so these copies
replace their runtime counterparts there (scons/web_assets.py); native bundles
use the shared runtime export without these browser crop/font derivatives. Regenerate them whenever a source changes; the build falls back to
the original file while `sources.json` does not match it, and
test/build_system/test_web_assets.py fails.

- sans-core.ttf: data/fonts/sans.ttf without the CJK outlines appended from Droid
  Sans Fallback (see data/fonts/README.md), keeping every original DejaVu glyph,
  its hinting and layout tables, plus the characters of every language's own name
  for the language list. The full font arrives later in the `font-cjk` package.
- Menu images use the shared pinned lossy/lossless WebP selection policy. The wordmark
  keeps the menu crop plus a margin; the unseen area is a flat fill.

Needs fontTools 4.64.0 and the pinned exporter interpreter:

    "$(python3 tools/package_assets.py --encoder-python)" browser/derive_assets.py
"""
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import shutil

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.package_assets import encode_image, image_recipe, encoder_ready, encoder_python
OUTPUT = ROOT / 'browser/assets'
FONT = 'data/fonts/sans.ttf'
COLONY = 'data/gfx/menu-colony.png'
WORDMARK = 'data/gfx/menu-wordmark.png'
# MainMenuScreen::loadWordmark's crop (x, y, w, h); keep a margin around it.
WORDMARK_CROP = (76, 232, 1956, 284)
WORDMARK_MARGIN = 8
# Every file that replaces a core file, with the source it is derived from.
DERIVED = {
    FONT: 'browser/assets/sans-core.ttf',
    COLONY: 'browser/assets/menu-colony.webp',
    WORDMARK: 'browser/assets/menu-wordmark.webp',
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def language_names(root):
    """Characters of each translation's own [language] and [language incomplete] values."""
    characters = set()
    for path in sorted((Path(root) / 'data').glob('texts.*.txt')):
        lines = path.read_text(encoding='utf-8').split('\n')
        for index, line in enumerate(lines[:-1]):
            if line in ('[language]', '[language incomplete]'):
                characters.update(lines[index + 1])
    return characters


def font_subset(root):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    source = Path(root) / FONT
    font = TTFont(source, recalcTimestamp=False)
    cmap = font.getBestCmap()
    keep = {cp for cp, name in cmap.items() if not name.startswith('cjk')}
    keep |= {ord(c) for c in language_names(root) if ord(c) in cmap}
    options = subset.Options()
    options.layout_features = ['*']
    options.name_IDs = ['*']
    options.name_languages = ['*']
    options.name_legacy = True
    options.legacy_kern = True
    options.hinting = True
    options.notdef_outline = True
    options.glyph_names = True
    options.recalc_timestamp = False
    options.drop_tables = []
    options.prune_unicode_ranges = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=sorted(keep))
    subsetter.subset(font)
    output = io.BytesIO()
    font.save(output)
    return output.getvalue()


def wordmark(root):
    from PIL import Image
    image = Image.open(Path(root) / WORDMARK)
    x, y, w, h = WORDMARK_CROP
    left, top = max(0, x - WORDMARK_MARGIN), max(0, y - WORDMARK_MARGIN)
    right = min(image.width, x + w + WORDMARK_MARGIN)
    bottom = min(image.height, y + h + WORDMARK_MARGIN)
    flat = Image.new(image.mode, image.size, image.getpixel((left, top)))
    flat.paste(image.crop((left, top, right, bottom)), (left, top))
    output = io.BytesIO()
    flat.save(output, 'PNG', optimize=True)
    return output.getvalue()


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if not encoder_ready():
        import subprocess
        return subprocess.call([encoder_python(), str(Path(__file__).resolve())])
    sources = {}
    # Target-keyed metadata accommodates whichever encoding wins.
    for source in DERIVED:
        if source == FONT:
            data = font_subset(ROOT)
            target = DERIVED[source]
            entry = {}
        else:
            with tempfile.TemporaryDirectory() as directory:
                reference = Path(directory) / 'reference.png'
                if source == WORDMARK:
                    reference.write_bytes(wordmark(ROOT))
                else:
                    shutil.copyfile(ROOT / source, reference)
                blob, record = encode_image(reference, source, ROOT / 'build/asset-cache', True)
                data = blob.read_bytes()
                target = 'browser/assets/' + Path(source).stem + record['suffix']
                entry = {'image_recipe': image_recipe(), 'lossy': record['lossy']}
            for suffix in ('.png', '.webp', '.jpg'):
                stale = OUTPUT / (Path(source).stem + suffix)
                if stale != ROOT / target:
                    stale.unlink(missing_ok=True)
        (ROOT / target).write_bytes(data)
        sources[target] = dict(entry, source=source, sha256=digest(ROOT / source),
                               output_sha256=hashlib.sha256(data).hexdigest())
        print(f'{target}: {len(data) / 1e6:.2f} MB (from {(ROOT / source).stat().st_size / 1e6:.2f} MB)')
    names = ''.join(sorted(language_names(ROOT)))
    sources['browser/assets/sans-core.ttf']['languageNames'] = hashlib.sha256(names.encode()).hexdigest()
    (OUTPUT / 'sources.json').write_text(json.dumps(sources, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    sys.exit(main())
