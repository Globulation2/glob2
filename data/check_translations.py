#!/usr/bin/env python3
"""Read-only validation and coverage reporting for the runtime string tables.

Run from any directory. --strict also fails on untranslated text; --json emits
per-key findings for translators. No mode rewrites the translation files.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
# Building descriptions occupy two separate rows. The second may be empty in
# any language, even when English needs both rows (and vice versa).
OPTIONAL_BLANKS = {
    f'[{name} explanation 2]' for name in (
        'barracks', 'clearingflag', 'defencetower', 'explorationflag', 'hospital',
        'inn', 'market', 'racetrack', 'school', 'stonewall', 'swarm',
        'swimmingpool', 'warflag')
}
KEY = re.compile(r'\[[^\r\n]+\]\Z')
PLACEHOLDER = re.compile(r'%[0-9]+')
LITERAL_LOOKUP = re.compile(r'getString(?:InLang)?\s*\(\s*"(\[[^"\n]*\])"')


def read_lines(path):
    # split on LF only: Unicode line separators are part of a runtime value.
    text = path.read_text(encoding='utf-8')
    return text.removesuffix('\n').split('\n') if text else []


def read_table(path):
    lines = read_lines(path)
    errors = []
    entries = {}
    if len(lines) % 2:
        errors.append(f'{path.name}: key without a value on line {len(lines)}')
    for index in range(0, len(lines) - 1, 2):
        key, value = lines[index:index + 2]
        if not KEY.fullmatch(key):
            errors.append(f'{path.name}:{index + 1}: malformed key {key!r}')
        if key in entries:
            errors.append(f'{path.name}:{index + 1}: duplicate key {key}')
        if '\r' in key or '\r' in value:
            errors.append(f'{path.name}:{index + 1}: unexpected carriage return')
        if '\ufffd' in value or '\x00' in value:
            errors.append(f'{path.name}:{index + 2}: invalid character in {key}')
        entries[key] = value
    return entries, errors


def placeholders(text):
    return Counter(PLACEHOLDER.findall(text))


def invalid_percent(text):
    # Match StringTable::load: following % only a digit, space, or % is legal.
    # A trailing % (e.g. "%0%") is a literal percentage sign.
    return bool(re.search(r'%[^0-9 %]', text))


def audit(root=ROOT):
    listing = read_lines(root / 'data/texts.list.txt')
    errors = []
    if not listing or listing[0] != 'data/texts.keys.txt':
        return {'errors': ['texts.list.txt must start with data/texts.keys.txt'], 'languages': {}}
    keys = read_lines(root / listing[0])
    for key, count in Counter(keys).items():
        if count > 1:
            errors.append(f'texts.keys.txt: duplicate key {key}')
        if not KEY.fullmatch(key) or invalid_percent(key):
            errors.append(f'texts.keys.txt: malformed key {key!r}')
    if len(set(listing)) != len(listing):
        errors.append('texts.list.txt: duplicate file')
    key_set = set(keys)
    tables = {}
    for name in listing[1:]:
        table, table_errors = read_table(root / name)
        errors.extend(table_errors)
        tables[name] = table
    english = tables.get('data/texts.en.txt', {})
    languages = {}
    codes = set()
    for name, table in tables.items():
        missing = sorted(key_set - table.keys())
        for key in missing:
            errors.append(f'{name}: missing key {key}')
        for key, value in table.items():
            if key in OPTIONAL_BLANKS and not value and english.get(key):
                first = key.replace(' explanation 2]', ' explanation]')
                if table.get(first):
                    errors.append(f'{name}: empty {key} would show an English second line')
            if value and (placeholders(key) != placeholders(value) or invalid_percent(value)):
                errors.append(f'{name}: invalid placeholders in {key}: {value!r}')
        code = table.get('[language-code]')
        if not code or code in codes:
            errors.append(f'{name}: missing or duplicate language code {code!r}')
        codes.add(code)
        for key in ('[language]', '[language incomplete]', '[language-tr]'):
            if not table.get(key):
                errors.append(f'{name}: missing language metadata {key}')
        untranslated = sorted(k for k in key_set if not table.get(k, '').strip() and k not in OPTIONAL_BLANKS)
        if name == 'data/texts.en.txt':
            errors.extend(f'{name}: missing English fallback {k}' for k in untranslated)
        languages[name] = {
            'missing': missing,
            'untranslated': untranslated,
            'extra': sorted(table.keys() - key_set),
            # This is a review queue, NOT an automatic mistranslation test:
            # names, key labels and loanwords legitimately match English.
            'same_as_english': sorted(k for k in key_set if table.get(k) and table.get(k) == english.get(k)) if name != 'data/texts.en.txt' else [],
            'placeholder_text': sorted(k for k,v in table.items() if '??' in v),
        }
    incomplete_path = root / 'data/texts.incomplete.txt'
    if incomplete_path.exists():
        incomplete = read_lines(incomplete_path)
        if [name.removeprefix('*') for name in incomplete] != listing[1:]:
            errors.append('texts.incomplete.txt: languages must match texts.list.txt in order')
        for name in incomplete:
            if not name.startswith('*') and languages.get(name, {}).get('untranslated'):
                errors.append(f'{name}: incomplete language is marked complete')
    # The explicit historical backlog allows existing unfinished languages in CI,
    # but never allows a newly blank translation to silently regress coverage.
    pending_path = root / 'data/translations.pending.json'
    if pending_path.exists():
        pending = json.loads(pending_path.read_text(encoding='utf-8'))
        for name, report in languages.items():
            for key in sorted(set(report['untranslated']) - set(pending.get(name, []))):
                errors.append(f'{name}: newly untranslated key {key}')
    # Catch literal lookups; dynamically composed building/event keys need
    # contextual review and are deliberately not guessed by this scanner.
    for directory in ('src', 'libgag'):
        for path in sorted((root / directory).rglob('*')):
            if path.suffix not in ('.cpp', '.h'):
                continue
            for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
                if line.lstrip().startswith('//'):
                    continue
                for key in LITERAL_LOOKUP.findall(line):
                    if key not in key_set:
                        errors.append(f'{path.relative_to(root)}:{number}: unknown translation key {key}')
    return {'errors': errors, 'languages': languages}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--strict', action='store_true', help='also fail on untranslated entries')
    parser.add_argument('--json', action='store_true', help='print per-key audit as JSON')
    args = parser.parse_args()
    try:
        result = audit()
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        parser.exit(1, f'Translation audit failed: {exc}\n')
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for name, report in result['languages'].items():
            print(f"{name}: {len(report['untranslated'])} untranslated, "
                  f"{len(report['missing'])} missing keys, {len(report['extra'])} obsolete keys")
        for error in result['errors']:
            print(f'ERROR: {error}')
        print(f"{len(result['errors'])} structural errors")
    incomplete = any(r['untranslated'] for r in result['languages'].values())
    return int(bool(result['errors']) or (args.strict and incomplete))


if __name__ == '__main__':
    raise SystemExit(main())
