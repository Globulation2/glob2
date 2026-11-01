#!/usr/bin/env python3
"""Prepare unregistered translation drafts and audit independently supplied review records.

Generated sources, drafts and review evidence belong under ignored artifacts/.
This tool never registers a locale, modifies live catalogs or creates sign-offs.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('translation_checker', ROOT / 'data/check_translations.py')
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
LOCALES = ('hi', 'bn', 'ur', 'mr', 'te', 'sw', 'ha', 'ta', 'fil', 'th', 'gu',
           'pa-Guru', 'pa-Arab', 'kn', 'am', 'yo', 'my', 'jv', 'ne', 'om',
           'pcm', 'ps', 'bho', 'fuc-Latn', 'ln', 'uz', 'ml', 'or', 'sd', 'ig', 'su')
SOURCE_PATHS = ('data/texts.keys.txt', 'data/texts.en.txt',
                'platform/packages/i18n/locales/en.json',
                'platform/packages/i18n/plural-sources.json',
                'platform/packages/i18n/invariants.json')
LOW_RESOURCE = {'ha', 'am', 'yo', 'my', 'jv', 'om', 'pcm', 'ps', 'bho', 'fuc-Latn',
                'ln', 'uz', 'sd', 'ig', 'su'}
GATES = ('language_brief', 'source_based_pilot', 'structural', 'desktop_rendering', 'phone_rendering', 'browser_startup',
         'native_platforms', 'browser_platforms', 'mobile_platforms')
CONTEXT = {
    'tutorial': 'Teach the player the action shown. Preserve tutorial execution and mission identifiers; translate display text only.',
    'settings': 'Labels and help describe settings. Prefer short labels and explain their effect plainly.',
    'maps': 'Procedural terrain controls: passes are mountain routes, reach is stream length, and banks/bars are terrain, not finance/weapons.',
    'colony': 'Workers autonomously gather/build; explorers scout; warriors fight. Flags assign work or military attention, not direct unit movement.',
    'online': 'Account, multiplayer, upload and browser workflows. Preserve URLs, licences, paths, format tokens and product names.',
    'general': 'Read neighbouring source entries and inspect usage before translating ambiguous short labels.',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def game_code(locale):
    return 'fuc' if locale == 'fuc-Latn' else locale.lower()


def sources(root=ROOT):
    keys = CHECKER.read_lines(root / SOURCE_PATHS[0])
    native, errors = CHECKER.read_table(root / SOURCE_PATHS[1])
    if errors or len(keys) != len(set(keys)) or set(keys) != set(native):
        raise ValueError('English native catalog/key contract is invalid: ' + '; '.join(errors))
    online = json.loads((root / SOURCE_PATHS[2]).read_text(encoding='utf-8'))
    hashes = {name: digest((root / name).read_bytes()) for name in SOURCE_PATHS}
    source_hash = digest(json.dumps(hashes, sort_keys=True).encode())
    return keys, native, online, hashes, source_hash


def domain(surface, key):
    text = key.lower()
    if 'tutorial' in text:
        return 'tutorial'
    if surface == 'online':
        return 'online'
    if any(word in text for word in ('settings ', 'shortcut', 'keyboard', 'theme ', 'music ')):
        return 'settings'
    if any(word in text for word in ('generator', 'terrain', 'map ', 'island', 'river', 'mountain', 'lake', 'sandbar')):
        return 'maps'
    if any(word in text for word in ('worker', 'warrior', 'explorer', 'building', 'flag', 'swarm', 'inn ', 'market', 'fruit')):
        return 'colony'
    return 'general'


def prepare(output, batch_size=160, root=ROOT, locales=LOCALES):
    locales = tuple(locales)
    if not locales or len(locales) != len(set(locales)) or set(locales) - set(LOCALES):
        raise ValueError('Select distinct supported draft locales')
    keys, native, online, hashes, source_hash = sources(root)
    manifest_path = output / 'manifest.json'
    if manifest_path.exists():
        old = json.loads(manifest_path.read_text(encoding='utf-8'))
        if old['source_sha256'] != source_hash or old['batch_size'] != batch_size or old['locales'] != list(locales):
            raise ValueError('Existing kit differs; create a new output directory to retain its evidence.')
        return old
    groups = defaultdict(list)
    for surface, catalog in (('native', native), ('online', online)):
        for key, value in catalog.items():
            groups[(surface, domain(surface, key))].append({'id': surface + ':' + key, 'key': key, 'source': value})
    batches = []
    for (surface, category), entries in sorted(groups.items()):
        for start in range(0, len(entries), batch_size):
            batch_id = f'{surface}-{category}-{start // batch_size + 1:03d}'
            path = 'batches/' + batch_id + '.json'
            payload = {'id': batch_id, 'source_sha256': source_hash, 'surface': surface,
                       'domain': category, 'context': CONTEXT[category], 'entries': entries[start:start + batch_size]}
            write_json(output / path, payload)
            batches.append({'id': batch_id, 'path': path, 'keys': [entry['id'] for entry in payload['entries']]})
    try:
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        revision = None
    manifest = {'schema_version': 1, 'revision': revision, 'source_sha256': source_hash,
                'source_files': hashes, 'batch_size': batch_size, 'native_keys': len(keys),
                'online_keys': len(online), 'locales': list(locales), 'batches': batches}
    write_json(output / 'source/native.json', native)
    write_json(output / 'source/online.json', online)
    (output / 'source/native-keys.txt').write_text('\n'.join(keys) + '\n', encoding='utf-8')
    for locale in locales:
        write_json(output / 'reviews' / (locale + '.json'), {
            'locale': locale, 'source_sha256': source_hash, 'catalog_sha256': {},
            'batches': [], 'findings': [], 'language_brief': {
                'variety': '', 'script': '', 'region': '', 'sources': [],
                'terminology_rationales': [], 'unresolved_uncertainty': [],
                'certification': 'Agent-reviewed; not native-human-certified'},
            'gates': {gate: {'passed': False, 'evidence': []} for gate in GATES},
        })
    write_json(manifest_path, manifest)
    return manifest


def plural_categories(locale):
    # Intl is the same plural-rule authority used by the platform checker.
    result = subprocess.check_output(['node', '-e',
        'if (!Intl.PluralRules.supportedLocalesOf([process.argv[1]]).length) process.exit(2); '
        'process.stdout.write(JSON.stringify(new Intl.PluralRules(process.argv[1]).resolvedOptions().pluralCategories))', locale], text=True)
    return json.loads(result)


def draft(output, locale):
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if locale not in manifest['locales']:
        raise ValueError('Locale is not part of this kit')
    folder = output / 'drafts' / locale
    if folder.exists():
        raise ValueError('Draft already exists; refusing to overwrite translation work')
    keys = (output / 'source/native-keys.txt').read_text(encoding='utf-8').splitlines()
    online = json.loads((output / 'source/online.json').read_text(encoding='utf-8'))
    categories = plural_categories(locale)
    native_path = folder / ('texts.' + game_code(locale) + '.txt')
    native_path.parent.mkdir(parents=True)
    native_path.write_text(''.join(key + '\n\n' for key in keys), encoding='utf-8')
    write_json(folder / (locale + '.json'), {key: '' if isinstance(value, str) else {category: '' for category in categories}
                                          for key, value in online.items()})
    return folder


def catalog_paths(output, locale):
    return {'native': output / 'drafts' / locale / ('texts.' + game_code(locale) + '.txt'),
            'online': output / 'drafts' / locale / (locale + '.json')}


def check_draft(output, locale):
    """Draft-only contract check; official validators still gate registered catalogs."""
    native_source = json.loads((output / 'source/native.json').read_text(encoding='utf-8'))
    online_source = json.loads((output / 'source/online.json').read_text(encoding='utf-8'))
    paths = catalog_paths(output, locale)
    native, errors = CHECKER.read_table(paths['native'])
    online = json.loads(paths['online'].read_text(encoding='utf-8'))
    untranslated, source_equal = [], []
    categories = plural_categories(locale)
    for surface, source, target in (('native', native_source, native), ('online', online_source, online)):
        if set(source) != set(target):
            errors.append(surface + ': missing/extra keys')
        for key, original in source.items():
            value = target.get(key)
            label = surface + ':' + key
            if surface == 'native':
                if not isinstance(value, str):
                    errors.append(label + ': string expected')
                    continue
                variants = {'other': value}
                expected_tokens = CHECKER.placeholders(original)
                token_count = CHECKER.placeholders
                if CHECKER.invalid_percent(value):
                    errors.append(label + ': invalid percent token')
            else:
                if isinstance(original, str):
                    if not isinstance(value, str):
                        errors.append(label + ': string expected')
                        continue
                    variants = {'other': value}
                else:
                    if not isinstance(value, dict) or not set(categories).issubset(value):
                        errors.append(label + ': required plural variants missing')
                        continue
                    variants = value
                token_count = lambda text: Counter(re.findall(r'\{([a-zA-Z][a-zA-Z0-9_]*)\}', text))
                expected_tokens = token_count(key)
            for category, text in variants.items():
                item = label + ('/' + category if category != 'other' else '')
                if not isinstance(text, str):
                    errors.append(item + ': non-string translation')
                elif not text.strip():
                    if surface != 'native' or key not in CHECKER.OPTIONAL_BLANKS:
                        untranslated.append(item)
                elif token_count(text) != expected_tokens:
                    errors.append(item + ': placeholder mismatch')
                elif '\ufffd' in text or '\x00' in text:
                    errors.append(item + ': invalid character')
                elif text == (original if isinstance(original, str) else original.get(category)):
                    source_equal.append(item)
    return {'locale': locale, 'errors': errors, 'untranslated': untranslated,
            'same_as_english_review_queue': source_equal,
            'limits': 'Does not certify meaning or replace official source, invariant, font and browser checks.'}


def batch_target_hashes(manifest, catalogs):
    native, errors = CHECKER.read_table(catalogs['native'])
    if errors:
        raise ValueError('; '.join(errors))
    online = json.loads(catalogs['online'].read_text(encoding='utf-8'))
    values = {'native': native, 'online': online}
    return {batch['id']: digest(json.dumps({key: values[key.split(':', 1)[0]].get(key.split(':', 1)[1])
                                           for key in batch['keys']}, sort_keys=True, ensure_ascii=False).encode())
            for batch in manifest['batches']}


def audit_locale(output, manifest, record, locale):
    paths = catalog_paths(output, locale)
    issues = audit_review(manifest, record, paths, output)
    if record.get('locale') != locale:
        issues.append('review locale does not match the requested locale')
    try:
        checked = check_draft(output, locale)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        return issues + ['cannot validate draft: ' + str(exc)]
    issues.extend(checked['errors'])
    if checked['untranslated']:
        issues.append(str(len(checked['untranslated'])) + ' untranslated draft entries')
    accepted = record.get('accepted_english', [])
    approved = {entry.get('key') for entry in accepted if isinstance(entry, dict)
                and isinstance(entry.get('reason'), str) and entry['reason'].strip()}
    if len(approved) != len(accepted) or approved - set(checked['same_as_english_review_queue']):
        issues.append('English exceptions require exact matching entry IDs and individual rationales')
    unapproved = set(checked['same_as_english_review_queue']) - approved
    if unapproved:
        issues.append(str(len(unapproved)) + ' English matches lack individual reviewed exceptions')
    return issues


def audit_review(manifest, record, catalogs, evidence_root=None):
    """Check provenance, complete coverage and separation; never infer review from text."""
    issues = []
    if record.get('locale') not in manifest['locales']:
        issues.append('review locale is not in source manifest')
    if record.get('source_sha256') != manifest['source_sha256']:
        issues.append('review source hash is stale')
    hashes = {surface: digest(path.read_bytes()) for surface, path in catalogs.items() if path.is_file()}
    if len(hashes) != 2 or record.get('catalog_sha256') != hashes:
        issues.append('review catalog hashes are missing or stale')
    try:
        target_hashes = batch_target_hashes(manifest, catalogs)
    except (OSError, ValueError, KeyError) as exc:
        target_hashes = {}
        issues.append('cannot hash target batches: ' + str(exc))
    expected = {batch['id']: set(batch['keys']) for batch in manifest['batches']}
    records = record.get('batches', [])
    ids = [batch.get('id') for batch in records]
    if Counter(ids) != Counter(expected.keys()):
        issues.append('every source batch must have exactly one review record')
    for batch in records:
        name = batch.get('id')
        if name not in expected:
            continue
        people = [batch.get(role) for role in ('author', 'reviewer', 'editor')]
        if any(not isinstance(person, str) or not person.strip() for person in people) or len(set(people)) != 3:
            issues.append(f'{name}: author, reviewer and editor must be distinct identities')
        if batch.get('source_sha256') != manifest['source_sha256'] or batch.get('target_sha256') != target_hashes.get(name):
            issues.append(f'{name}: source/catalog revision is stale')
        for field in ('reviewed_keys', 'edited_keys', 'rechecked_keys'):
            values = batch.get(field, [])
            if len(values) != len(set(values)) or set(values) != expected[name]:
                issues.append(f'{name}: {field} must cover every key exactly once')
        if batch.get('rechecked_by') != batch.get('reviewer'):
            issues.append(f'{name}: independent reviewer must recheck the final edit')
    if record.get('locale') in LOW_RESOURCE:
        for batch in records:
            adversary = batch.get('adversarial_reviewer')
            if not adversary or adversary in [batch.get(role) for role in ('author', 'reviewer', 'editor')]:
                issues.append(str(batch.get('id')) + ': low-resource pass needs a reviewer independent of author/reviewer/editor')
            if set(batch.get('adversarial_reviewed_keys', [])) != expected.get(batch.get('id'), set()):
                issues.append(str(batch.get('id')) + ': adversarial pass must cover every key')
    brief = record.get('language_brief', {})
    if any(not brief.get(field) for field in ('variety', 'script', 'region', 'sources', 'terminology_rationales')):
        issues.append('language brief lacks variety/script/region, sources or terminology rationales')
    if brief.get('unresolved_uncertainty'):
        issues.append('unresolved linguistic uncertainty')
    all_keys = set().union(*expected.values())
    for finding in record.get('findings', []):
        if finding.get('key') not in all_keys or finding.get('severity') not in ('critical', 'major', 'minor'):
            issues.append('finding has an unknown key or severity')
        if finding.get('state') != 'resolved' and not (finding.get('severity') == 'minor' and
                finding.get('state') == 'accepted' and finding.get('reason', '').strip()):
            issues.append('unresolved finding: ' + str(finding.get('key')))
    for gate in GATES:
        result = record.get('gates', {}).get(gate, {})
        if result.get('passed') is not True or not result.get('evidence'):
            issues.append(gate + ' is pending or lacks evidence')
            continue
        if result.get('source_sha256') != manifest['source_sha256'] or result.get('catalog_sha256') != hashes:
            issues.append(gate + ': evidence is not bound to the final source/catalog hashes')
        for item in result['evidence']:
            if not isinstance(item, dict) or not item.get('path') or not item.get('sha256'):
                issues.append(gate + ': evidence requires path and sha256')
                continue
            path = Path(item['path'])
            if not path.is_absolute():
                path = (evidence_root or Path.cwd()) / path
            if not path.is_file() or digest(path.read_bytes()) != item['sha256']:
                issues.append(gate + ': evidence is missing or changed: ' + item['path'])
    return issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'draft', 'status', 'check-draft'))
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/translation-program')
    parser.add_argument('--locale', choices=LOCALES)
    parser.add_argument('--locales', nargs='+', choices=LOCALES, help='limit a new prepare kit to these locales')
    parser.add_argument('--batch-size', type=int, default=160)
    args = parser.parse_args()
    try:
        if args.batch_size < 1:
            raise ValueError('Batch size must be positive')
        if args.command == 'prepare':
            manifest = prepare(args.output, args.batch_size, locales=args.locales or LOCALES)
            print(json.dumps({key: manifest[key] for key in ('revision', 'source_sha256', 'native_keys', 'online_keys')}))
        elif args.command == 'draft':
            if not args.locale:
                raise ValueError('draft requires --locale')
            print(draft(args.output, args.locale))
        elif args.command == 'check-draft':
            if not args.locale:
                raise ValueError('check-draft requires --locale')
            result = check_draft(args.output, args.locale)
            print(json.dumps(result, ensure_ascii=False, indent=2))
            return int(bool(result['errors'] or result['untranslated']))
        else:
            manifest = json.loads((args.output / 'manifest.json').read_text(encoding='utf-8'))
            current_hash = sources()[4]
            result = {'source_current': current_hash == manifest['source_sha256'], 'locales': {}}
            for locale in ([args.locale] if args.locale else manifest['locales']):
                record = json.loads((args.output / 'reviews' / (locale + '.json')).read_text(encoding='utf-8'))
                result['locales'][locale] = audit_locale(args.output, manifest, record, locale)
            print(json.dumps(result, ensure_ascii=False, indent=2))
            return int(not result['source_current'] or any(result['locales'].values()))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
