#!/usr/bin/env python3
"""Export Chinese UI text and tutorial translation gaps for content review."""
import argparse
import csv
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'data'))
from check_translations import read_table  # noqa: E402

SHOW = re.compile(r'^\s*show\("((?:\\.|[^"\\])*)"(?:,\s*([a-z][a-z-]*))?\)\s*$')


def rows():
    table, errors = read_table(ROOT / 'data/texts.zh-cn.txt')
    if errors:
        raise ValueError('\n'.join(errors))
    result = [('ui', 'data/texts.zh-cn.txt', '', key, value, '')
              for key, value in sorted(table.items())]
    missing = 0
    for path in sorted((ROOT / 'scripts').glob('tutorial_part*.sgsl')):
        messages = []
        for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            if line.lstrip().startswith('#'):
                continue
            match = SHOW.match(line)
            if not match:
                continue
            source, language = match.groups()
            if language is None:
                messages.append([number, source, ''])
            elif language == 'zh-cn' and messages:
                messages[-1][2] = source
        for number, source, chinese in messages:
            if not chinese:
                missing += 1
            result.append(('tutorial', str(path.relative_to(ROOT)), number,
                           source, chinese, 'missing zh-cn' if not chinese else ''))
    return result, missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--strict', action='store_true', help='fail if tutorial translations are missing')
    args = parser.parse_args()
    result, missing = rows()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', newline='', encoding='utf-8') as output:
        writer = csv.writer(output, delimiter='\t')
        writer.writerow(('section', 'path', 'line', 'key_or_english', 'zh_cn', 'review_status'))
        writer.writerows(result)
    print(f'{len(result)} rows; {missing} tutorial messages missing zh-cn: {args.output}')
    return 1 if args.strict and missing else 0


if __name__ == '__main__':
    sys.exit(main())
