#!/usr/bin/env python3
"""Append CJK outlines to the original sans.ttf; see README.md for inputs."""
import argparse
import hashlib
from pathlib import Path

from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont


def build(base, donor, output):
    for path, expected in (
        (base, "b2a4e6a6e22c20ecf06c3178139ef55cad40ae9291431a5f6f52f28614e473e4"),
        (donor, "21b96a0377f067833a93af3082eb28d4ffab7a8cd46bfd513286f1d64b7b0949"),
    ):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Unexpected source font: {path}")
    font = TTFont(base, recalcTimestamp=False)
    source = TTFont(donor, recalcTimestamp=False)
    existing = font.getBestCmap()
    # CJK radicals, punctuation, kana, bopomofo, unified/compatibility
    # ideographs, and fullwidth forms. Include all donor coverage in these
    # blocks, not just the characters currently used by the translations.
    additions = {
        cp: name for cp, name in source.getBestCmap().items()
        if cp not in existing and (
            0x2E80 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF
            or 0xFF00 <= cp <= 0xFFEF or 0x20000 <= cp <= 0x2FA1F
        )
    }
    glyph_set = source.getGlyphSet()
    scale = font['head'].unitsPerEm / source['head'].unitsPerEm
    order = font.getGlyphOrder()[:]
    for cp, name in sorted(additions.items()):
        new_name = f"cjk{cp:05X}"
        recording = DecomposingRecordingPen(glyph_set)
        glyph_set[name].draw(recording)
        pen = TTGlyphPen(None)
        recording.replay(TransformPen(pen, (scale, 0, 0, scale, 0, 0)))
        # Donor hint programs depend on donor-global tables. Import only
        # outlines; keep every original glyph and its hinting untouched.
        font['glyf'][new_name] = pen.glyph()
        width, bearing = source['hmtx'][name]
        font['hmtx'][new_name] = (round(width * scale), round(bearing * scale))
        order.append(new_name)
        for table in font['cmap'].tables:
            if table.isUnicode() and (cp <= 0xFFFF or table.format in (12, 13)):
                table.cmap[cp] = new_name
    font.setGlyphOrder(order)
    # Preserve original line metrics and all layout tables. Give the modified
    # font its own family name, and retain both upstream license notices.
    names = {1: 'Glob2 Sans', 3: 'Glob2 Sans CJK 1.0', 4: 'Glob2 Sans',
             5: 'Version 1.0; DejaVu 2.26 with Droid CJK outlines',
             6: 'Glob2Sans', 16: 'Glob2 Sans'}
    for record in font['name'].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    copyright_notice = font['name'].getDebugName(0) + (
        '\nCJK outlines: Digitized data copyright Google Corporation © 2006.'
        '\nModified for Globulation 2: scaled and appended CJK outlines.'
    )
    license_notice = font['name'].getDebugName(13) + (
        '\n\nCJK outlines licensed under the Apache License, Version 2.0.'
        '\nSee accompanying LICENSE-Droid.txt and LICENSE-DejaVu.txt.'
    )
    for record in font['name'].names:
        if record.nameID in (0, 13):
            value = copyright_notice if record.nameID == 0 else license_notice
            record.string = value.encode(record.getEncoding())
    font.save(output)
    print(f"Added {len(additions)} CJK characters to {output}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('donor', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build(args.base, args.donor, args.output)
