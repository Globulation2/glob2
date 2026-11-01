#!/usr/bin/env python3
"""Prepare unregistered translation drafts and audit independently supplied review records.

Generated sources, drafts and review evidence belong under ignored artifacts/.
This tool never registers a locale, modifies live catalogs or creates sign-offs.
"""
import argparse
from contextlib import contextmanager
from collections import Counter, defaultdict
from decimal import Decimal, InvalidOperation
try:
    import fcntl
except ImportError:  # Ordinary prepare/check/audit commands also run on Windows.
    fcntl = None
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import os
import time
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('translation_checker', ROOT / 'data/check_translations.py')
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
LOCALES = ('hi', 'bn', 'ur', 'mr', 'te', 'sw', 'ha', 'ta', 'fil', 'th', 'gu',
           'pa-Guru', 'pa-Arab', 'kn', 'am', 'yo', 'my', 'jv', 'ne', 'om',
           'pcm', 'ps', 'bho', 'fuc-Latn', 'ln', 'uz', 'ml', 'or', 'sd', 'ig', 'su', 'bg')
# Existing registered native languages are opt-in compatibility drafting scopes.
# LOCALES and prepare()'s default retain the original translation-program scope.
LEGACY_NATIVE_LOCALES = (
    'ar', 'pt-BR', 'ca', 'cs', 'da', 'de', 'eo', 'es', 'eu', 'fa', 'fi', 'fr',
    'el', 'hi', 'hu', 'id', 'it', 'ja', 'ko', 'nl', 'pl', 'pt-PT', 'ro', 'ru',
    'sl', 'sk', 'sr-Cyrl', 'sv', 'sw', 'tr', 'uk', 'ur', 'vi', 'zh-Hans', 'zh-Hant',
)
SUPPORTED_DRAFT_LOCALES = tuple(dict.fromkeys(LOCALES + LEGACY_NATIVE_LOCALES))
NATIVE_GAME_CODES = {
    'fuc-Latn': 'fuc', 'cs': 'cz', 'da': 'dk', 'el': 'gr', 'sl': 'si',
    'pt-BR': 'br', 'sr-Cyrl': 'sr', 'zh-Hans': 'zh-cn', 'zh-Hant': 'zh-tw', 'pt-PT': 'pt',
}
# Explicitly reviewed Google general/nmt targets only. Do not derive these from
# BCP-47 prefixes: a valid draft locale can still have an unsupported script.
# https://docs.cloud.google.com/translate/docs/languages#nmt
# Serbian sr is the NMT target for native Cyrillic; Chinese uses API region codes.
# Native br is Brazilian Portuguese (not Breton); native pt is European Portuguese.
# Both Portuguese variants deliberately use their reviewed regional API targets.
NMT_TARGETS = {
    'bg': 'bg', 'ar': 'ar', 'pt-BR': 'pt-BR', 'ca': 'ca', 'cs': 'cs', 'da': 'da',
    'de': 'de', 'eo': 'eo', 'es': 'es', 'eu': 'eu', 'fa': 'fa', 'fi': 'fi',
    'fr': 'fr', 'el': 'el', 'hi': 'hi', 'hu': 'hu', 'id': 'id', 'it': 'it',
    'ja': 'ja', 'ko': 'ko', 'nl': 'nl', 'pl': 'pl', 'pt-PT': 'pt-PT',
    'ro': 'ro', 'ru': 'ru', 'sl': 'sl', 'sk': 'sk', 'sr-Cyrl': 'sr',
    'sv': 'sv', 'sw': 'sw', 'tr': 'tr', 'uk': 'uk', 'ur': 'ur', 'vi': 'vi',
    'zh-Hans': 'zh-CN', 'zh-Hant': 'zh-TW',
}
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
    return NATIVE_GAME_CODES.get(locale, locale.lower())


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
    if not locales or len(locales) != len(set(locales)) or set(locales) - set(SUPPORTED_DRAFT_LOCALES):
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
    for name in ('plural-sources.json', 'invariants.json'):
        (output / 'source' / name).write_bytes((root / 'platform/packages/i18n' / name).read_bytes())
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
    manifest['source_snapshots'] = {path.name: digest(path.read_bytes()) for path in (output / 'source').iterdir() if path.is_file()}
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
    if record.get('locale') in LOW_RESOURCE or record.get('locale') == 'bg':
        for batch in records:
            adversary = batch.get('adversarial_reviewer')
            if not adversary or adversary in [batch.get(role) for role in ('author', 'reviewer', 'editor')]:
                issues.append(str(batch.get('id')) + ': adversarial pass needs a reviewer independent of author/reviewer/editor')
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


def atomic_json(path, value):
    """Replace complete state, including durable budget reservations before network IO."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    directory = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


@contextmanager
def machine_lock(output):
    if fcntl is None:
        raise ValueError('machine-draft requires POSIX file locking; use Linux or macOS')
    output.mkdir(parents=True, exist_ok=True)
    with (output / '.machine-draft.lock').open('a') as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise ValueError('Another machine-draft process holds this kit lock') from exc
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


# Replace protected source spans with numbered, ASCII-only markers. Each marker
# must survive exactly once; never guess how a provider changed a protected span.
PROTECTED = re.compile(
    r'U\+[0-9A-Fa-f]{4,6}|https?://[^\s<>]+|<[^>]+>|\{[a-zA-Z][a-zA-Z0-9_]*\}|%[0-9]+|%%|'
    r'`[^`]+`|\b[A-Za-z_$][\w$]*(?:[./_][\w$-]+)+\b|'
    r'\b[a-z]+[A-Z][A-Za-z0-9]*\b|\b(?:[A-Z][a-z]*){2,}\b')


def protect_text(source, invariants=()):
    spans = [(m.start(), m.end()) for m in PROTECTED.finditer(source)]
    for invariant in invariants:
        if not isinstance(invariant, str) or not invariant:
            raise ValueError('Invalid frozen invariant')
        spans.extend((m.start(), m.end()) for m in re.finditer(re.escape(invariant), source))
    merged = []
    for start, end in sorted(spans):
        if merged and start < merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((start, end))
    prefix = 'ZXQ' + digest(source.encode())[:12].upper() + 'Q'
    if prefix in source:
        raise ValueError('Protected marker collides with source')
    replacements = {}
    result, position = [], 0
    for index, (start, end) in enumerate(merged):
        marker = prefix + str(index) + 'QXZ'
        replacements[marker] = source[start:end]
        result.extend((source[position:start], marker))
        position = end
    result.append(source[position:])
    return ''.join(result), replacements


def restore_text(translated, replacements):
    if not isinstance(translated, str) or not translated.strip() or any(c in translated for c in ('\n', '\r', '\x00', '\ufffd')):
        raise ValueError('Provider returned blank, multiline or invalid translation')
    for marker in replacements:
        if translated.count(marker) != 1:
            raise ValueError('Provider damaged protected marker: ' + marker)
    for marker, original in replacements.items():
        translated = translated.replace(marker, original)
    if re.search(r'ZXQ[A-F0-9]{12}Q\d+QXZ', translated):
        raise ValueError('Provider returned unexpected protected marker')
    return translated


def machine_draft(output, locale, project, max_cost_usd='10', rate_per_million='20', retries=2, limit_entries=None, only_keys=None):
    """Google Translation Advanced NMT draft; paid calls are conservatively reserved.

    A reservation is never refunded, including HTTP errors, timeouts and crashes.
    The ledger is shared by all locales/projects in a kit. Cache hits are free.
    """
    if locale not in NMT_TARGETS:
        raise ValueError('No reviewed Google NMT target for locale: ' + str(locale))
    if not project or not re.fullmatch(r'[a-z][a-z0-9-]{4,61}[a-z0-9]|[0-9]+', project):
        raise ValueError('A valid Google Cloud project ID or number is required')
    try:
        cap, rate = Decimal(str(max_cost_usd)), Decimal(str(rate_per_million))
        if not cap.is_finite() or not rate.is_finite() or cap <= 0 or rate <= 0:
            raise ValueError('Cost cap and rate must be finite positive amounts')
    except InvalidOperation as exc:
        raise ValueError('Invalid cost cap or rate') from exc
    if limit_entries is not None and limit_entries < 1:
        raise ValueError('Entry limit must be positive')
    with machine_lock(output):
        return _machine_draft(output, locale, project, cap, rate, retries, limit_entries, only_keys)


def _machine_draft(output, locale, project, cap, rate, retries, limit_entries, only_keys):
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if locale not in manifest['locales']:
        raise ValueError('Locale is not part of this kit')
    snapshots = manifest.get('source_snapshots')
    if not snapshots:
        raise ValueError('Kit lacks frozen source contracts; prepare a fresh kit')
    for name, expected in snapshots.items():
        if digest((output / 'source' / name).read_bytes()) != expected:
            raise ValueError('Frozen source contract changed: ' + name)
    native = json.loads((output / 'source/native.json').read_text(encoding='utf-8'))
    online = json.loads((output / 'source/online.json').read_text(encoding='utf-8'))
    invariants = json.loads((output / 'source/invariants.json').read_text(encoding='utf-8'))
    if not isinstance(invariants, dict):
        raise ValueError('Frozen invariants must be an object')
    paths = catalog_paths(output, locale)
    if not paths['native'].parent.exists():
        draft(output, locale)
    edited_native, errors = CHECKER.read_table(paths['native'])
    edited_online = json.loads(paths['online'].read_text(encoding='utf-8'))
    categories = plural_categories(locale)
    if errors or set(edited_native) != set(native) or set(edited_online) != set(online):
        raise ValueError('Editable draft has malformed or mismatched catalog keys')
    selected = None
    if only_keys is not None:
        if not isinstance(only_keys, list) or any(not isinstance(key, str) for key in only_keys) or len(set(only_keys)) != len(only_keys):
            raise ValueError('only-keys must be a JSON list of distinct entry IDs')
        selected = set(only_keys)
        available = {'native:' + key for key in native} | {'online:' + key for key in online}
        if selected - available:
            raise ValueError('Unknown only-keys IDs: ' + ', '.join(sorted(selected - available)))
    items = []
    for surface, source, edited in (('native', native, edited_native), ('online', online, edited_online)):
        for key, value in source.items():
            if selected is not None and surface + ':' + key not in selected:
                continue
            variants = {'': value} if isinstance(value, str) else {
                category: value.get(category, value.get('other')) for category in categories}
            if not isinstance(value, str) and (not isinstance(edited[key], dict) or set(edited[key]) != set(categories)):
                raise ValueError('Editable draft plural schema differs: ' + key)
            for category, text in variants.items():
                current = edited[key][category] if category else edited[key]
                if not isinstance(current, str) or not isinstance(text, str):
                    raise ValueError('Catalog translation/source must be a string: ' + key)
                if not current.strip() and text.strip():
                    if limit_entries is not None and len(items) >= limit_entries:
                        continue
                    protected, replacements = protect_text(text, invariants.get(key, []) if surface == 'online' else [])
                    if len(protected) > 5000:
                        raise ValueError('Protected source exceeds 5000 codepoints; split source explicitly: ' + key)
                    items.append({'surface': surface, 'key': key, 'category': category, 'source': text,
                                  'protected': protected, 'replacements': replacements})
    if limit_entries is not None:
        items = items[:limit_entries]
    folder = output / 'machine' / locale
    folder.mkdir(parents=True, exist_ok=True)
    ledger_path = output / 'machine/spend-ledger.json'
    ledger = json.loads(ledger_path.read_text()) if ledger_path.exists() else {'schema_version': 1, 'reservations': []}
    spent = sum((Decimal(item['reserved_usd']) for item in ledger['reservations']), Decimal(0))
    if spent > cap:
        raise ValueError('Existing conservative spend already exceeds requested cap')
    machine_path = folder / 'provider-drafts.json'
    provider_drafts = json.loads(machine_path.read_text()) if machine_path.exists() else {}
    token = None
    requests = 0
    completed = 0
    batches, batch, size = [], [], 0
    for item in items:
        if batch and size + len(item['protected']) > 5000:
            batches.append(batch)
            batch, size = [], 0
        batch.append(item)
        size += len(item['protected'])
    if batch:
        batches.append(batch)
    for batch in batches:
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': NMT_TARGETS[locale], 'mimeType': 'text/plain',
                   'model': f'projects/{project}/locations/global/models/general/nmt',
                   'contents': [item['protected'] for item in batch]}
        endpoint = f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText'
        cache_key = digest(json.dumps({'endpoint': endpoint, 'payload': payload}, sort_keys=True, ensure_ascii=False).encode())
        cache = folder / 'cache' / (cache_key + '.json')
        if cache.exists():
            response = json.loads(cache.read_text())
        else:
            if token is None:
                token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True).strip()
                if not token or any(c.isspace() for c in token):
                    raise ValueError('gcloud returned invalid access token')
            charge = Decimal(sum(len(item['protected']) for item in batch)) * rate / Decimal(1000000)
            for attempt in range(retries + 1):
                if spent + charge > cap:
                    raise ValueError(f'Cost cap would be exceeded: reserved ${spent}, next ${charge}, cap ${cap}')
                reservation = {'request_sha256': cache_key, 'project': project, 'locale': locale,
                               'codepoints': sum(len(item['protected']) for item in batch),
                               'rate_per_million': str(rate), 'reserved_usd': str(charge), 'state': 'reserved'}
                ledger['reservations'].append(reservation)
                atomic_json(ledger_path, ledger)
                spent += charge
                requests += 1
                raw_path = folder / 'raw' / f'{cache_key}-{len(ledger["reservations"]):06d}'
                atomic_json(raw_path.with_suffix('.request.json'), {'endpoint': endpoint, 'payload': payload})
                request = urllib.request.Request(endpoint, data=json.dumps(payload).encode(), headers={
                    'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json; charset=utf-8',
                    'x-goog-user-project': project}, method='POST')
                try:
                    with urllib.request.urlopen(request, timeout=90) as result:
                        raw = result.read()
                    raw_path.with_suffix('.response.bin').write_bytes(raw)
                    response = json.loads(raw)
                    reservation['state'] = 'response-received'
                    atomic_json(ledger_path, ledger)
                    break
                except (urllib.error.URLError, TimeoutError) as exc:
                    if isinstance(exc, urllib.error.HTTPError):
                        try:
                            raw_path.with_suffix('.response.bin').write_bytes(exc.read())
                        finally:
                            exc.close()
                    reservation['state'] = 'failed-or-ambiguous'
                    reservation['error_type'] = type(exc).__name__
                    atomic_json(ledger_path, ledger)
                    retryable = not isinstance(exc, urllib.error.HTTPError) or exc.code in (429, 500, 502, 503, 504)
                    if attempt == retries or not retryable:
                        raise ValueError('Translation request failed; reservation retained: ' + type(exc).__name__) from exc
                    time.sleep(min(2 ** attempt, 4))
        translations = response.get('translations') if isinstance(response, dict) else None
        if not isinstance(translations, list) or len(translations) != len(batch):
            raise ValueError('Malformed provider response: translation count/order contract violated')
        restored = []
        for item, translation in zip(batch, translations):
            if not isinstance(translation, dict) or not isinstance(translation.get('translatedText'), str):
                raise ValueError('Malformed provider translation')
            restored.append(restore_text(translation['translatedText'], item['replacements']))
        # Only validated responses enter the reusable cache. Raw evidence is retained
        # even when JSON/schema/marker validation fails.
        atomic_json(cache, response)
        # Editors may work while the provider request is in flight. Re-read the
        # editable catalogs at each checkpoint and fill only still-blank values.
        edited_native, errors = CHECKER.read_table(paths['native'])
        edited_online = json.loads(paths['online'].read_text(encoding='utf-8'))
        if errors or set(edited_native) != set(native) or set(edited_online) != set(online):
            raise ValueError('Editable draft changed to malformed/mismatched catalog keys')
        for key, value in online.items():
            current = edited_online[key]
            if isinstance(value, str):
                valid = isinstance(current, str)
            else:
                valid = isinstance(current, dict) and set(current) == set(categories) and all(isinstance(v, str) for v in current.values())
            if not valid:
                raise ValueError('Editable draft changed to invalid online schema: ' + key)
        for item, translated in zip(batch, restored):
            identifier = json.dumps([item['surface'], item['key'], item['category']], ensure_ascii=False)
            provider_drafts[identifier] = {'source': item['source'], 'translation': translated, 'request_sha256': cache_key}
            target = edited_native if item['surface'] == 'native' else edited_online
            if item['category']:
                if not target[item['key']][item['category']].strip():
                    target[item['key']][item['category']] = translated
            elif not target[item['key']].strip():
                target[item['key']] = translated
            completed += 1
        atomic_json(machine_path, provider_drafts)
        # Checkpoint each successful request so interruption leaves resumable work.
        native_text = ''.join(key + '\n' + edited_native[key] + '\n' for key in native)
        temporary = paths['native'].with_suffix('.tmp')
        temporary.write_text(native_text, encoding='utf-8')
        os.replace(temporary, paths['native'])
        atomic_json(paths['online'], edited_online)
    return {'locale': locale, 'filled': completed, 'paid_requests': requests, 'reserved_usd': str(spent),
            'draft_folder': str(paths['native'].parent), 'provider_folder': str(folder)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'draft', 'status', 'check-draft', 'machine-draft'))
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/translation-program')
    parser.add_argument('--locale', choices=SUPPORTED_DRAFT_LOCALES)
    parser.add_argument('--locales', nargs='+', choices=SUPPORTED_DRAFT_LOCALES, help='limit a new prepare kit to these locales')
    parser.add_argument('--batch-size', type=int, default=160)
    parser.add_argument('--only-keys', type=Path, help='JSON list of source entry IDs for a representative pilot')
    parser.add_argument('--limit-entries', type=int, help='fill at most this many blank entries for a bounded pilot')
    parser.add_argument('--project', help='Google Cloud project for paid machine drafting')
    parser.add_argument('--max-cost-usd', default='10')
    parser.add_argument('--rate-per-million', default='20')
    args = parser.parse_args()
    try:
        if args.batch_size < 1:
            raise ValueError('Batch size must be positive')
        if args.command == 'prepare':
            manifest = prepare(args.output, args.batch_size, locales=args.locales or LOCALES)
            print(json.dumps({key: manifest[key] for key in ('revision', 'source_sha256', 'native_keys', 'online_keys')}))
        elif args.command == 'machine-draft':
            print(json.dumps(machine_draft(args.output, args.locale, args.project, args.max_cost_usd, args.rate_per_million, limit_entries=args.limit_entries, only_keys=json.loads(args.only_keys.read_text(encoding='utf-8')) if args.only_keys else None), ensure_ascii=False))
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
