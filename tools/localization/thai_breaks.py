#!/usr/bin/env python3
"""Suggest Thai word-break hints using a development-only pinned dictionary.

The generated catalog must receive editorial review; dictionary segmentation is
not linguistic approval. No Python package or dictionary ships with the game.
"""
import argparse
import json
from pathlib import Path
import re

THAI = re.compile(r'[\u0e00-\u0e7f]+')
# Reviewed game terms that dictionary context can otherwise split internally.
# This is a suggestion guard, not a substitute for reviewing surrounding words.
PROTECTED_TERMS = ('คนงาน', 'นักสำรวจ', 'พื้นที่', 'กราเดียนต์', 'ดรัมลิน',
                   'ฟยอร์ด', 'อัปโหลด', 'ดาวน์โหลด', 'แผนที่', 'กำลัง', 'ป้องกัน',
                   'เนื้อหา', 'โหลด', 'แอป', 'เซสชัน', 'มิลลิวินาที', 'เกรเดียนต์',
                   'โมเรน', 'เอเวอร์เกลดส์')


def annotate(text):
    from pythainlp.tokenize import word_tokenize

    def words(match):
        source = match.group()
        tokens = word_tokenize(source, engine='newmm', keep_whitespace=True)
        if ''.join(tokens) != source:
            raise ValueError('Segmenter changed catalog characters')
        ends, position = set(), 0
        for token in tokens[:-1]:
            position += len(token)
            ends.add(position)
        forbidden = set()
        for term in PROTECTED_TERMS:
            start = source.find(term)
            while start >= 0:
                forbidden.update(range(start + 1, start + len(term)))
                start = source.find(term, start + 1)
        return ''.join(character + ('\u200b' if index + 1 in ends and index + 1 not in forbidden else '')
                       for index, character in enumerate(source))

    return THAI.sub(words, text.replace('\u200b', ''))


def catalog(text):
    lines = text.split('\n')
    if lines[-1] != '' or (len(lines) - 1) % 2:
        raise ValueError('Expected alternating key/value lines and a final newline')
    for index in range(0, len(lines) - 1, 2):
        if not re.fullmatch(r'\[[^\r\n]+\]', lines[index]):
            raise ValueError('Invalid catalog key')
        lines[index + 1] = annotate(lines[index + 1])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use distinct input/output files; review output before replacing input')
    import pythainlp
    args.output.write_text(catalog(args.input.read_text(encoding='utf-8')), encoding='utf-8')
    print(json.dumps({'locale': 'th', 'pythainlp': pythainlp.__version__, 'engine': 'newmm',
                      'output': str(args.output)}))


if __name__ == '__main__':
    main()
