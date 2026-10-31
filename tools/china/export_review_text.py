#!/usr/bin/env python3
"""Export Chinese UI text and tutorial translation gaps for content review."""
import argparse
import csv
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'data'))
from check_translations import read_table, lesson_translation_keys  # noqa: E402



def rows():
    table, errors = read_table(ROOT / 'data/texts.zh-cn.txt')
    if errors:
        raise ValueError('\n'.join(errors))
    result = [('ui', 'data/texts.zh-cn.txt', '', key, value, '')
              for key, value in sorted(table.items())]
    missing = 0
    english, errors = read_table(ROOT / 'data/texts.en.txt')
    if errors:
        raise ValueError('\n'.join(errors))
    for definition, keys in lesson_translation_keys(ROOT).items():
        for key in sorted(keys):
            chinese = table.get(key, '')
            if not chinese:
                missing += 1
            result.append(('tutorial', definition, '', english.get(key, key),
                           chinese, 'missing zh-cn' if not chinese else ''))
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
