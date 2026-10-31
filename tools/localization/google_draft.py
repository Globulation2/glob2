#!/usr/bin/env python3
"""Budget-limited Google NMT drafts for an existing translation-program kit.

No credentials are persisted. API results are comparison drafts, never approvals.
Only blank draft entries are filled; reviewed/nonblank work is never overwritten.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.error
import urllib.request

SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
MAX_CHARACTERS = 400_000  # US$8 at US$20/million, without assuming free credits.
MODEL = 'general/nmt'
PROTECTION_VERSION = 1
TOKENS = re.compile(r'%[0-9]+|\{[A-Za-z][A-Za-z0-9_]*\}|https?://[^\s<>]+|`[^`\n]+`|\\[nrt]|(?:Ctrl|Alt|Shift|Cmd)\+[A-Za-z0-9+]+')
BRANDS = ('Globulation 2', 'Globulation2', 'JavaScript', 'SDL', 'Hive Mind')
SENTINEL = re.compile(r'__G2P[0-9]+__')
_ACCESS_TOKEN = None
_TOKEN_OBTAINED_AT = 0.0


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


@contextmanager
def locked(folder):
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / '.lock').open('a') as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def protect(text, literals=()):
    if SENTINEL.search(text):
        raise ValueError('Source contains reserved protection marker')
    # Longest literal wins; exact invariants come from the frozen source contract.
    alternatives = [re.escape(item) for item in sorted(set(literals) | set(BRANDS), key=len, reverse=True) if item]
    pattern = re.compile('|'.join(alternatives + [TOKENS.pattern]))
    protected = []

    def replace(match):
        marker = f'__G2P{len(protected)}__'
        protected.append(match.group())
        return marker

    return pattern.sub(replace, text), protected


def restore(text, protected):
    expected = Counter(f'__G2P{index}__' for index in range(len(protected)))
    if Counter(SENTINEL.findall(text)) != expected:
        raise ValueError('Translation damaged protected literals; retain response for manual repair')
    result = SENTINEL.sub(lambda match: protected[int(match.group()[5:-2])], text)
    if any(char in result for char in ('\n', '\r', '\x00', '\ufffd')):
        raise ValueError('Translation contains unsupported control/invalid characters')
    if not result.strip():
        raise ValueError('Translation is empty')
    return result


def restore_entry(text, entry):
    result = restore(text, entry['protected'])
    # Some fragments are concatenated with adjacent UI values. Google may trim
    # their surrounding spaces; preserve the authored source boundary spacing.
    leading = re.match(r'^[ \t]*', entry['source']).group()
    trailing = re.search(r'[ \t]*$', entry['source']).group()
    return leading + result.strip(' \t') + trailing


def load_kit(output, locale):
    manifest = json.loads((output / 'manifest.json').read_text())
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in this kit')
    source = {name: json.loads((output / 'source' / (name + '.json')).read_text()) for name in ('native', 'online')}
    # The manifest hashes canonical files; source snapshots must still match them.
    for surface, original in [('native', PROGRAM.SOURCE_PATHS[1]), ('online', PROGRAM.SOURCE_PATHS[2])]:
        expected = PROGRAM.CHECKER.read_table(PROGRAM.ROOT / original)[0] if surface == 'native' else json.loads((PROGRAM.ROOT / original).read_text())
        if source[surface] != expected:
            raise ValueError('Frozen source differs from current source; prepare a new kit')
    _, _, _, hashes, source_hash = PROGRAM.sources()
    if source_hash != manifest['source_sha256'] or hashes != manifest['source_files']:
        raise ValueError('Source contracts changed; prepare a new kit')
    paths = PROGRAM.catalog_paths(output, locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    targets = {'native': native, 'online': json.loads(paths['online'].read_text())}
    invariants = json.loads((PROGRAM.ROOT / 'platform/packages/i18n/invariants.json').read_text())
    return manifest, source, targets, paths, invariants


def pending_entries(source, targets, invariants, selected=None):
    entries = []
    for surface, catalog in source.items():
        if set(catalog) != set(targets[surface]):
            raise ValueError(surface + ': draft key contract differs from source')
        for key, value in catalog.items():
            item_id = surface + ':' + key
            if selected is not None and item_id not in selected:
                continue
            variants = {None: value} if isinstance(value, str) else value
            target = targets[surface][key]
            if isinstance(value, str) != isinstance(target, str):
                raise ValueError(item_id + ': draft type differs')
            for category in ({None} if isinstance(value, str) else target.keys()):
                current = target if category is None else target[category]
                if current.strip():
                    continue
                text = variants.get(category, variants.get('other'))
                if not text.strip():
                    continue  # Optional blank game entries are intentionally blank.
                masked, literals = protect(text, invariants.get(key, ()) if surface == 'online' else ())
                entries.append({'surface': surface, 'key': key, 'category': category, 'source': text,
                                'content': masked, 'protected': literals})
    return entries


def cache_key(entry, project, locale):
    return digest({'project': project, 'target': locale, 'source': 'en', 'model': MODEL,
                   'mimeType': 'text/plain', 'version': PROTECTION_VERSION,
                   'content': entry['content'], 'protected': entry['protected']})


def batches(entries, max_chars=4500, max_entries=64):
    batch, characters = [], 0
    for entry in entries:
        size = len(entry['content'])
        if size > max_chars:
            raise ValueError('Single message exceeds request character limit: ' + entry['key'])
        if batch and (characters + size > max_chars or len(batch) >= max_entries):
            yield batch
            batch, characters = [], 0
        batch.append(entry)
        characters += size
    if batch:
        yield batch


def access_token():
    global _ACCESS_TOKEN, _TOKEN_OBTAINED_AT
    now = time.monotonic()
    if _ACCESS_TOKEN is None or now - _TOKEN_OBTAINED_AT >= 45 * 60:
        token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True, timeout=90).strip()
        if not token:
            raise ValueError('gcloud returned an empty access token')
        _ACCESS_TOKEN = token
        _TOKEN_OBTAINED_AT = now
    return _ACCESS_TOKEN


def request_api(project, payload):
    token = access_token()
    request = urllib.request.Request(
        f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText',
        data=json.dumps(payload).encode(), headers={'Authorization': 'Bearer ' + token,
            'Content-Type': 'application/json; charset=utf-8', 'x-goog-user-project': project})
    with urllib.request.urlopen(request, timeout=90) as response:
        return json.load(response)


def translate(folder, entries, project, locale, limit=MAX_CHARACTERS, dispatch=request_api, execute=False):
    if not re.fullmatch(r'[a-z][a-z0-9-]{4,61}[a-z0-9]', project):
        raise ValueError('An explicit valid Google project ID is required')
    if not 0 < limit <= MAX_CHARACTERS:
        raise ValueError('Budget must be positive and cannot exceed 400000 characters / US$8')
    folder.mkdir(parents=True, exist_ok=True)
    ledger_path = folder / 'ledger.json'
    ledger = json.loads(ledger_path.read_text()) if ledger_path.exists() else {
        'project': project, 'limit_characters': limit, 'price_usd_per_million': 20, 'attempts': []}
    if ledger['project'] != project or ledger['limit_characters'] != limit:
        raise ValueError('Existing ledger project/budget differs; do not reset the campaign budget')
    spent = sum(attempt['characters_reserved'] for attempt in ledger['attempts'])
    unique, results, repair_required = {}, {}, []
    for entry in entries:
        identity = cache_key(entry, project, locale)
        path = folder / 'cache' / (identity + '.json')
        if path.exists():
            cached = json.loads(path.read_text())
            if cached['identity'] != identity or cached['source'] != entry['source']:
                raise ValueError('Cache provenance mismatch')
            try:
                results[identity] = restore_entry(cached['translatedText'], entry)
            except ValueError as error:
                repair_required.append({'surface': entry['surface'], 'key': entry['key'],
                    'category': entry['category'], 'cache': str(path), 'reason': str(error)})
        else:
            unique.setdefault(identity, entry)
    required = sum(len(entry['content']) for entry in unique.values())
    summary = {'project': project, 'locale': locale, 'pending_entries': len(entries),
               'uncached_unique_messages': len(unique), 'characters_reserved': spent,
               'new_request_characters': required, 'worst_case_total_usd': (spent + required) * 20 / 1_000_000,
               'limit_characters': limit, 'execute': execute, 'repair_required': repair_required}
    if spent + required > limit:
        raise ValueError('Insufficient remaining budget: ' + json.dumps(summary))
    if not execute:
        return summary, {}
    # Persist the ceiling before credentials/network access. Never refund failures:
    # a timed-out request might have reached the provider and incurred charges.
    atomic_json(ledger_path, ledger)
    for batch in batches(list(unique.values())):
        cost = sum(len(entry['content']) for entry in batch)
        if spent + cost > limit:
            raise ValueError('Budget exhausted before dispatch')
        number = len(ledger['attempts']) + 1
        attempt = {'number': number, 'characters_reserved': cost,
                   'utc': datetime.now(timezone.utc).isoformat(), 'state': 'reserved', 'locale': locale}
        ledger['attempts'].append(attempt)
        atomic_json(ledger_path, ledger)
        spent += cost
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': locale, 'mimeType': 'text/plain',
                   'model': f'projects/{project}/locations/global/models/{MODEL}',
                   'contents': [entry['content'] for entry in batch]}
        request_path = folder / 'requests' / f'{number:05d}.json'
        atomic_json(request_path, {'project': project, 'payload': payload,
                    'entries': [{'identity': cache_key(entry, project, locale), **entry} for entry in batch]})
        try:
            response = dispatch(project, payload)
            atomic_json(folder / 'responses' / f'{number:05d}.json', response)
            values = response.get('translations', [])
            if len(values) != len(batch):
                raise ValueError('Response count differs from request count')
            for entry, value in zip(batch, values):
                translated = value.get('translatedText')
                if not isinstance(translated, str):
                    raise ValueError('Missing translatedText in response')
                identity = cache_key(entry, project, locale)
                # Keep even damaged output cached: rerunning should not silently
                # pay to regenerate it. A reviewer must repair the original draft.
                atomic_json(folder / 'cache' / (identity + '.json'), {'identity': identity,
                            'source': entry['source'], 'translatedText': translated, 'attempt': number})
            # Cache the whole valid response shape before validating literals so
            # one damaged message cannot force payment for its batch peers again.
            for entry, value in zip(batch, values):
                identity = cache_key(entry, project, locale)
                try:
                    results[identity] = restore_entry(value['translatedText'], entry)
                except ValueError as error:
                    repair_required.append({'surface': entry['surface'], 'key': entry['key'],
                        'category': entry['category'], 'cache': str(folder / 'cache' / (identity + '.json')),
                        'reason': str(error)})
            attempt['state'] = 'completed'
        except Exception as error:
            attempt['state'] = 'failed_or_uncertain'
            attempt['error_type'] = type(error).__name__
            # HTTP bodies are useful diagnostics and contain no request credentials.
            if isinstance(error, urllib.error.HTTPError):
                atomic_json(folder / 'responses' / f'{number:05d}-error.json',
                            {'status': error.code, 'body': error.read().decode('utf-8', errors='replace')})
            atomic_json(ledger_path, ledger)
            raise
        atomic_json(ledger_path, ledger)
    summary['characters_reserved'] = spent
    return summary, results


def apply_results(paths, entries, results, project, locale):
    # Re-read immediately before applying, preserving any edits since estimation.
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    targets = {'native': native, 'online': json.loads(paths['online'].read_text())}
    for entry in entries:
        catalog = targets[entry['surface']]
        key, category = entry['key'], entry['category']
        value = catalog[key] if category is None else catalog[key][category]
        if value.strip():
            continue
        identity = cache_key(entry, project, locale)
        if identity not in results:
            continue  # Damaged provider output is retained for manual repair.
        result = results[identity]
        if category is None:
            catalog[key] = result
        else:
            catalog[key][category] = result
    temporary = paths['native'].with_suffix('.tmp')
    temporary.write_text(''.join(key + '\n' + value + '\n' for key, value in native.items()), encoding='utf-8')
    os.replace(temporary, paths['native'])
    atomic_json(paths['online'], targets['online'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing translation-program kit')
    parser.add_argument('--project', required=True)
    parser.add_argument('--locale', required=True, choices=PROGRAM.LOCALES)
    parser.add_argument('--keys', type=Path, help='JSON array of native:/online: IDs for a pilot')
    parser.add_argument('--max-characters', type=int, default=MAX_CHARACTERS)
    parser.add_argument('--execute', action='store_true', help='Actually call the paid API and fill blank draft entries')
    args = parser.parse_args()
    folder = args.output / 'google-nmt'
    try:
        with locked(folder):
            manifest, source, targets, paths, invariants = load_kit(args.output, args.locale)
            selected = json.loads(args.keys.read_text()) if args.keys else None
            if selected is not None:
                all_keys = {surface + ':' + key for surface, catalog in source.items() for key in catalog}
                if not isinstance(selected, list) or set(selected) - all_keys:
                    raise ValueError('Pilot contains unknown entry IDs')
            entries = pending_entries(source, targets, invariants, selected)
            summary, results = translate(folder, entries, args.project, args.locale,
                                         args.max_characters, execute=args.execute)
            if args.execute:
                apply_results(paths, entries, results, args.project, args.locale)
            print(json.dumps(summary, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
