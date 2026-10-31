#!/usr/bin/env python3
"""Read-only validation and coverage reporting for the runtime string tables.

Run from any directory. --strict also fails on unapproved untranslated text; --json emits
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
# Tokenize comments, literals and punctuation together so comments and quoted
# examples cannot masquerade as code. Adjacent C++ string literals are joined.
CPP_TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*|[^\s]', re.S)


def cpp_tokens(text):
    tokens = []
    line, previous_end = 1, 0
    for match in CPP_TOKEN.finditer(text):
        line += text.count("\n", previous_end, match.start())
        previous_end = match.start()
        token = match.group()
        if token.startswith(('//', '/*')):
            continue
        value = None
        if token.startswith('R"'):
            value = token[token.index('(') + 1:-(len(match['delimiter']) + 2)]
        elif token.startswith('"'):
            # Translation keys use plain UTF-8 with occasional escaped quotes.
            value = re.sub(r'\\(["\\])', r'\1', token[1:-1])
        if value is not None and tokens and tokens[-1][1] is not None:
            previous = tokens.pop()
            tokens.append((previous[0] + token, previous[1] + value, previous[2]))
        else:
            tokens.append((token, value, line))
    return tokens


def argument_groups(tokens, start):
    """Return top-level arguments of a call or aggregate initializer."""
    pairs = {'(': ')', '{': '}', '[': ']'}
    stack = [pairs[tokens[start][0]]]
    groups, group = [], []
    for token in tokens[start + 1:]:
        spelling = token[0]
        if spelling == stack[-1]:
            stack.pop()
            if not stack:
                return groups + [group]
        elif spelling in pairs:
            stack.append(pairs[spelling])
        if spelling == ',' and len(stack) == 1:
            groups.append(group)
            group = []
        else:
            group.append(token)
    return []


def expression_literals(tokens):
    """Literal branches at expression level, excluding nested call arguments."""
    depth = 0
    for spelling, value, line in tokens:
        if spelling in ('(', '{', '['):
            depth += 1
        elif spelling in (')', '}', ']'):
            depth -= 1
        elif depth == 0 and value is not None:
            yield value, line


def source_keys(path):
    """Find literal keys, including tables/wrappers, and settings conventions.

    This is deliberately a lexical contract, not a C++ evaluator. Computed IDs,
    authored content and hard-coded UI prose still need contextual review.
    """
    tokens = cpp_tokens(path.read_text(encoding='utf-8'))
    found = {(value, line) for _, value, line in tokens
             if value is not None and KEY.fullmatch(value)}
    for index, (name, _, _) in enumerate(tokens[:-1]):
        prefix = {'musicText': 'music ', 'customAIText': '', 'libraryText': 'library '}.get(name)
        if prefix is None or tokens[index + 1][0] != '(':
            continue
        args = argument_groups(tokens, index + 1)
        if args:
            for value, line in expression_literals(args[0]):
                if value and not any(c in value for c in '[]'):
                    found.add(('[' + prefix + value + ']', line))
    if path.name.startswith('SettingsScreen') and 'settings' in path.parts:
        # These helpers translate label/help through SettingsScreen::tr.
        positions = {'tr': (0,), 'section': (0,), 'toggle': (1, 2),
                     'choice': (1, 2), 'effect': (1, 2),
                     'appearance': (1, 2, 4, 5), 'themeChoice': (1, 2),
                     'ScrollSlider': (1, 2)}
        for index, (name, _, _) in enumerate(tokens[:-1]):
            if name not in positions or tokens[index + 1][0] not in ('(', '{'):
                continue
            if index and tokens[index - 1][0] == ':':
                continue  # Glob2UI::tr takes complete keys, not settings prose.
            args = argument_groups(tokens, index + 1)
            for position in positions[name]:
                if position < len(args):
                    for value, line in expression_literals(args[position]):
                        if value and not any(c in value for c in '[]'):
                            found.add(('[settings ' + value + ']', line))
        # Category names are translated by indexing this fixed array.
        for index, (name, _, _) in enumerate(tokens[:-4]):
            if name == 'names' and [t[0] for t in tokens[index + 1:index + 5]] == ['[', ']', '=', '{']:
                for group in argument_groups(tokens, index + 4):
                    for _, value, line in group:
                        if value:
                            found.add(('[settings ' + value + ']', line))
    if path.name == 'MetricCatalog.cpp':
        for index, token in enumerate(tokens[:-3]):
            if token[0] == '.' and tokens[index + 1][0] == 'id' and tokens[index + 2][0] == '=':
                _, value, line = tokens[index + 3]
                if value:
                    found.update((('[stat ' + value + suffix + ']', line)
                                  for suffix in ('', ' about')))
    if path.name == 'ExperimentalFeatures.cpp':
        for index, token in enumerate(tokens[:-6]):
            if token[0] == '{' and tokens[index + 1][0] == 'ExperimentId':
                # {ExperimentId::Name, "stable-id", "label", "help"}
                args = argument_groups(tokens, index)
                if len(args) >= 4 and len(args[1]) == 1 and args[1][0][1]:
                    _, value, line = args[1][0]
                    found.update((('[experiment ' + value + suffix + ']', line)
                                  for suffix in ('', ' help')))
    return sorted(found, key=lambda item: (item[1], item[0]))



def source_ui_literals(path):
    """Find direct prose in frontend controls, action tables and map overlays.

    These argument positions belong to the frontend helpers. Other subsystems
    can have unrelated functions with the same names, so scope them explicitly.
    """
    if not any(part in path.parts for part in ('ui', 'hud', 'hive', 'screens', 'overlay')):
        return []
    positions = {'button': 1, 'paragraph': 0, 'caption': 0, 'label': 0,
                 'page': 0, 'heading': 0, 'field': 0, 'drawString': 3}
    technical = {'.opus', 'CC0-1.0', 'CC-BY-4.0', 'CC-BY-SA-4.0'}
    tokens = cpp_tokens(path.read_text(encoding='utf-8'))
    found = set()

    def inspect(expression):
        for value, line in expression_literals(expression):
            plain = re.sub(r'\\(?:x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4})', '', value)
            if value not in technical and re.search(r'[A-Za-z]{2}', plain) and not value.startswith('['):
                found.add((value, line))

    for index, (name, _, _) in enumerate(tokens[:-2]):
        if name == 'actions':
            start = index + 1
            if tokens[start][0] in ('(', '='):
                start += 1
            if tokens[start][0] == '{':
                for entry in argument_groups(tokens, start):
                    if entry and entry[0][0] == '{':
                        args = argument_groups(entry, 0)
                        if len(args) > 1:
                            inspect(args[1])
        if tokens[index + 1][0] != '(':
            continue
        if name not in positions and name != 'choice':
            continue
        args = argument_groups(tokens, index + 1)
        if name == 'choice':
            if len(args) > 1 and args[1] and args[1][0][0] == '{':
                inspect(args[1][1:-1])
        elif positions[name] < len(args):
            inspect(args[positions[name]])
    return sorted(found, key=lambda item: (item[1], item[0]))


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
    # Explicitly accepted English fallbacks stay visible in coverage reports.
    # They do not exempt missing keys, malformed text or untranslated English.
    pending_path = root / 'data/texts.pending.txt'
    pending = set(read_lines(pending_path)) if pending_path.exists() else set()
    for key in sorted(pending - key_set):
        errors.append(f'texts.pending.txt: unknown pending key {key}')
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
            'pending': sorted(set(untranslated) & pending) if name != 'data/texts.en.txt' else [],
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
    source_files = source_references = 0
    # Full literal tokens catch keys passed through wrappers, arrays, ternaries,
    # multiline calls and adjacent literals as well as direct getString calls.
    for directory in ('src', 'libgag'):
        for path in sorted((root / directory).rglob('*')):
            if path.suffix not in ('.cpp', '.h', '.hpp', '.cc', '.cxx'):
                continue
            if 'Test' in path.stem or 'Harness' in path.stem:
                continue
            references = source_keys(path)
            for text, number in source_ui_literals(path):
                errors.append(f'{path.relative_to(root)}:{number}: unlocalized UI text {text!r}')
            source_files += 1
            source_references += len(references)
            for key, number in references:
                if key not in key_set:
                    errors.append(f'{path.relative_to(root)}:{number}: unknown translation key {key}')
    themes = root / 'data/themes/index.json'
    if themes.exists():
        try:
            theme_ids = json.loads(themes.read_text(encoding='utf-8'))['themes']
            for theme in theme_ids:
                key = '[theme ' + theme + ']'
                if key not in key_set:
                    errors.append(f'data/themes/index.json: unknown translation key {key}')
        except (ValueError, KeyError, TypeError) as error:
            errors.append(f'data/themes/index.json: {error}')
    # Ruleset names and descriptions are keys looked up at runtime from the data file.
    rulesets = root / 'data' / 'rulesets.json'
    if rulesets.exists():
        try:
            entries = json.loads(rulesets.read_text(encoding='utf-8')).get('rulesets', [])
        except ValueError as error:
            entries = []
            errors.append(f'rulesets.json: {error}')
        for entry in entries:
            if not isinstance(entry, dict):
                errors.append(f'rulesets.json: ruleset entry is not an object: {entry!r}')
                continue
            for field in ('name', 'description'):
                key = entry.get(field)
                if key not in key_set:
                    errors.append(f'rulesets.json: unknown translation key {key!r} ({field} of {entry.get("id")!r})')
    tutorials = {}
    # SGSL is a separate authored-content localization system. Inventory it
    # explicitly; catalog completeness must never imply tutorial completeness.
    for path in sorted((root / 'scripts').glob('tutorial_part*.sgsl')):
        counts = Counter()
        for line in path.read_text(encoding='utf-8').splitlines():
            match = re.match(r'\s*show\(".*"\s*(?:,\s*([a-z-]+))?\s*\)', line)
            if match:
                counts[match[1] or 'en'] += 1
        tutorials[str(path.relative_to(root))] = {
            'message_counts': dict(sorted(counts.items())),
            'legacy_languages_without_messages': sorted(codes - counts.keys()),
            'display_catalog_gaps': {name: [f'[tutorial part{path.stem.removeprefix("tutorial_part")} message {i}]'
                for i in range(1, counts['en'] + 1)
                if not table.get(f'[tutorial part{path.stem.removeprefix("tutorial_part")} message {i}]', '').strip()]
                for name, table in tables.items()},
        }
    coverage = {
        'native_source_files': source_files,
        'native_key_references': source_references,
        'checked_key_families': ['literal bracketed C++ keys', 'settings helpers and categories',
            'musicText', 'customAIText', 'libraryText', 'statistics metrics', 'experiments',
            'shipped theme IDs', 'ruleset names and descriptions', 'direct frontend UI prose'],
        'tutorials': tutorials,
        'limits': [
            'Lexical scanning does not prove every rendered string is translated; hard-coded prose and new wrappers require UI review.',
            'Computed generator, building, resource and keyboard names require contextual review.',
            'Online/platform locales and browser shell bindings use the platform i18n checker.',
            'SGSL coverage describes any custom authored scripts still present. Retired tutorial aliases are verified against test-only archives; active JS lesson keys are checked by the lesson authoring tool. Linguistic freshness still needs review.',
            'Font coverage, text fitting and visual layout require rendered language checks.',
        ],
    }
    return {'errors': errors, 'languages': languages, 'coverage': coverage}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--strict', action='store_true', help='also fail on untranslated entries outside texts.pending.txt')
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
            print(f"{name}: {len(report['untranslated'])} untranslated ({len(report['pending'])} pending), "
                  f"{len(report['missing'])} missing keys, {len(report['extra'])} obsolete keys")
        for error in result['errors']:
            print(f'ERROR: {error}')
        print(f"{len(result['errors'])} structural errors")
        print('Coverage limits (including tutorial languages): use --json for the full inventory.')
    incomplete = any(set(r['untranslated']) - set(r['pending']) for r in result['languages'].values())
    return int(bool(result['errors']) or (args.strict and incomplete))


if __name__ == '__main__':
    raise SystemExit(main())
