#!/usr/bin/env python3
"""Budgeted Google NMT drafts for a frozen translation_program kit.

Dry-run is the default. --run sends paid requests and fills only blank draft
entries. It never registers languages, overwrites editorial work or signs reviews.
All response/cache/budget evidence stays inside the kit's ignored directory.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
from decimal import Decimal
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request
import time

if os.name == 'nt':
    import msvcrt
else:
    import fcntl

SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
RATE_USD_PER_MILLION = Decimal('20')
MAX_BUDGET_USD = Decimal('9')
# Opaque ASCII sentinels are deliberately validated, never silently repaired.
SENTINEL = re.compile(r'ZXQPH[0-9]+QXZ')
NEWLINE = re.compile(r'((?:\\n|\r\n|\n|\r)+)')
TOKENS = re.compile(r'%[0-9]+|\{[a-zA-Z][a-zA-Z0-9_]*\}|https?://[^\s<>]+|[\r\n\t]|\\[nrt]')
TECHNICAL = ('Globulation 2', 'Glob2', 'Hive Mind', 'JavaScript', 'WebAssembly',
             'UTF-8', 'SHA-256', 'JSON', 'HTTP', 'HTTPS', 'CC BY-SA', 'CC BY', 'CC0')


def digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True).encode()).hexdigest()


def fsync_directory(path):
    # Windows has no portable directory-fsync equivalent in Python. File contents
    # are flushed there, but OS-crash durability of the rename is not guaranteed.
    if os.name == 'nt':
        return
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def atomic_text(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    data = content.encode('utf-8')
    with temporary.open('wb') as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)
    fsync_directory(path.parent)
    return hashlib.sha256(data).hexdigest()


def atomic_json(path, value):
    return atomic_text(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def mask(text, invariants=()):
    if SENTINEL.search(text):
        raise ValueError('Source collides with placeholder sentinel')
    literals = sorted(set(invariants) | set(TECHNICAL), key=len, reverse=True)
    pattern = re.compile('|'.join(re.escape(word) for word in literals if word) + '|' + TOKENS.pattern)
    protected = []

    def replace(match):
        token = f'ZXQPH{len(protected)}QXZ'
        protected.append((token, match.group()))
        return token

    return pattern.sub(replace, text), protected


def unmask(text, protected, source):
    expected = Counter(token for token, _ in protected)
    if Counter(SENTINEL.findall(text)) != expected:
        raise ValueError('Google response damaged protected tokens; inspect cached response')
    for token, original in protected:
        text = text.replace(token, original)
    # Preserve authored spaces around sentence fragments and rich-text slots.
    leading = re.match(r'^\s*', source).group()
    trailing = re.search(r'\s*$', source).group()
    text = leading + text.strip() + trailing if source.strip() else source
    if Counter(TOKENS.findall(text)) != Counter(TOKENS.findall(source)):
        raise ValueError('Restored translation has changed format tokens')
    if '\ufffd' in text or '\x00' in text:
        raise ValueError('Invalid character in Google response')
    if source.strip() and not text.strip():
        raise ValueError('Empty Google response')
    return text


def entries(output, locale):
    manifest = json.loads((output / 'manifest.json').read_text())
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in the frozen kit')
    invariants_path = PROGRAM.ROOT / 'platform/packages/i18n/invariants.json'
    if PROGRAM.digest(invariants_path.read_bytes()) != manifest['source_files']['platform/packages/i18n/invariants.json']:
        raise ValueError('Invariant contract changed since the kit was frozen')
    invariants = json.loads(invariants_path.read_text())
    paths = PROGRAM.catalog_paths(output, locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    online = json.loads(paths['online'].read_text())
    targets = {'native': native, 'online': online}
    result = []
    plural_categories = PROGRAM.plural_categories(locale)
    for batch in manifest['batches']:
        payload = json.loads((output / batch['path']).read_text())
        for entry in payload['entries']:
            surface, key, source = payload['surface'], entry['key'], entry['source']
            variants = source if isinstance(source, dict) else {None: source}
            target = targets[surface].get(key)
            categories = plural_categories if isinstance(source, dict) else [None]
            for category in categories:
                original = variants.get(category, variants.get('other'))
                value = target.get(category) if isinstance(target, dict) else target
                if value:
                    continue
                if not original.strip():
                    if category is None:
                        targets[surface][key] = original
                    else:
                        targets[surface][key][category] = original
                    continue
                masked, protected = mask(original, invariants.get(key, ()) if surface == 'online' else ())
                pieces = []
                for index, part in enumerate(NEWLINE.split(original)):
                    if index % 2 or not part.strip():
                        pieces.append(part)
                    else:
                        part_masked, part_protected = mask(part, invariants.get(key, ()) if surface == 'online' else ())
                        pieces.append({'source': part, 'masked': part_masked, 'protected': part_protected})
                result.append({'pieces': pieces, 'id': entry['id'], 'surface': surface, 'key': key, 'category': category,
                               'source': original, 'masked': masked, 'protected': protected,
                               'domain': surface + ':' + payload['domain']})
    return paths, targets, result


def item_contents(item):
    if 'pieces' not in item:
        return [item['masked']]
    return [piece['masked'] for piece in item['pieces'] if isinstance(piece, dict)]


def request_payload(items, locale):
    return {'sourceLanguageCode': 'en', 'targetLanguageCode': locale,
            'mimeType': 'text/plain', 'contents': [text for item in items for text in item_contents(item)]}


def batches(items, max_characters=4000, max_items=80):
    group, count, content_count = [], 0, 0
    for item in items:
        contents = item_contents(item)
        size = sum(map(len, contents))
        if size > max_characters or len(contents) > max_items:
            raise ValueError(f"Single source exceeds request character limit: {item['id']}")
        if group and (count + size > max_characters or content_count + len(contents) > max_items):
            yield group
            group, count, content_count = [], 0, 0
        group.append(item)
        count += size
        content_count += len(contents)
    if group:
        yield group


class Budget:
    """Ledger counts every dispatched attempt, including timeout/HTTP failures."""
    def __init__(self, path, project, ceiling):
        self.path, self.project = path, project
        self.ceiling = Decimal(str(ceiling))
        if not Decimal('0') < self.ceiling <= MAX_BUDGET_USD:
            raise ValueError('Budget must be greater than zero and at most US$9')
        self.value = json.loads(path.read_text()) if path.exists() else {
            'schema_version': 1, 'project': project, 'rate_usd_per_million': str(RATE_USD_PER_MILLION),
            'attempts': []}
        if self.value['project'] != project or self.value['rate_usd_per_million'] != str(RATE_USD_PER_MILLION):
            raise ValueError('Budget project/rate differs from existing ledger')

    @property
    def characters(self):
        return sum(attempt['characters'] for attempt in self.value['attempts'])

    @property
    def cost(self):
        return Decimal(self.characters) * RATE_USD_PER_MILLION / Decimal(1000000)

    def reserve(self, payload, cache_key):
        characters = sum(len(text) for text in payload['contents'])
        projected = self.cost + Decimal(characters) * RATE_USD_PER_MILLION / Decimal(1000000)
        if projected > self.ceiling:
            raise ValueError(f'Google request would exceed US${self.ceiling} budget; no request sent')
        self.value['attempts'].append({'id': len(self.value['attempts']) + 1, 'characters': characters,
                                      'request_sha256': cache_key, 'state': 'dispatched_or_uncertain'})
        atomic_json(self.path, self.value)
        return self.value['attempts'][-1]

    def complete(self, attempt, response):
        attempt.update(state='response_received', response_sha256=digest(response))
        atomic_json(self.path, self.value)


@contextmanager
def kit_lock(output):
    with (output / '.google-draft.lock').open('a+b') as lock:
        if os.name == 'nt':
            lock.seek(0, os.SEEK_END)
            if lock.tell() == 0:
                lock.write(b'0')
                lock.flush()
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == 'nt':
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


_access_token = None
_access_token_time = 0.0


def google_translate(project, payload):
    # A subprocess argv rather than shell expansion keeps the token out of logs.
    global _access_token, _access_token_time
    now = time.monotonic()
    if _access_token is None or now - _access_token_time > 2400:
        _access_token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True).strip()
        _access_token_time = now
    token = _access_token
    if not token:
        raise ValueError('gcloud returned an empty access token')
    endpoint = f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText'
    model = f'projects/{project}/locations/global/models/general/nmt'
    request = urllib.request.Request(endpoint, data=json.dumps({**payload, 'model': model}).encode(),
        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json',
                 'x-goog-user-project': project}, method='POST')
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def fetch(project, payload, cache, budget, translate=google_translate):
    identity = {'project': project, 'model': 'general/nmt', 'payload': payload}
    key = digest(identity)
    path = cache / (key + '.json')
    if path.exists():
        cached = json.loads(path.read_text())
        if cached.get('request') != identity or cached.get('response_sha256') != digest(cached['response']):
            raise ValueError('Corrupt Google response cache')
        return cached['response']
    attempt = budget.reserve(payload, key)
    # No implicit retry: reruns reserve another attempt when no response exists.
    response = translate(project, payload)
    atomic_json(path, {'request': identity, 'response': response, 'response_sha256': digest(response)})
    budget.complete(attempt, response)
    return response


def catalog_hashes(paths):
    return {surface: hashlib.sha256(path.read_bytes()).hexdigest()
            for surface, path in paths.items()}


def write_targets(paths, targets, expected_hashes):
    if catalog_hashes(paths) != expected_hashes:
        raise ValueError('Draft catalogs changed during this run; disk edits and cached responses retained')
    native = ''.join(key + '\n' + value + '\n' for key, value in targets['native'].items())
    for surface, content in (('native', native),
                             ('online', json.dumps(targets['online'], ensure_ascii=False, indent=2) + '\n')):
        # Editors do not participate in kit_lock. Detect changes before each
        # catalog write; an exclusive writer is still required around replacement.
        if catalog_hashes(paths) != expected_hashes:
            raise ValueError('Draft catalogs changed during this run; disk edits and cached responses retained')
        expected_hashes[surface] = atomic_text(paths[surface], content)


def pilot_entries(items, count=100):
    """Round-robin source domains rather than reviewing only the first menu."""
    groups = {}
    for item in items:
        groups.setdefault(item['domain'], []).append(item)
    selected = []
    while groups and len(selected) < count:
        for domain in list(groups):
            selected.append(groups[domain].pop(0))
            if not groups[domain]:
                del groups[domain]
            if len(selected) == count:
                break
    return selected


def run(output, locale, project, ceiling='9', limit=None, execute=False, translate=google_translate, pilot=False, surface=None):
    with kit_lock(output):
        expected_hashes = catalog_hashes(PROGRAM.catalog_paths(output, locale))
        paths, targets, items = entries(output, locale)
        if surface is not None:
            if surface not in ('native', 'online'):
                raise ValueError('Surface must be native or online')
            items = [item for item in items if item['surface'] == surface]
        if pilot:
            if limit is not None:
                raise ValueError('Choose either --pilot or --limit')
            items = pilot_entries(items)
        if limit is not None:
            if limit < 1:
                raise ValueError('Limit must be positive')
            items = items[:limit]
        budget = Budget(output / 'google-budget.json', project, ceiling)
        planned = list(batches(items))
        characters = sum(len(text) for item in items for text in item_contents(item))
        estimate = Decimal(characters) * RATE_USD_PER_MILLION / Decimal(1000000)
        summary = {'locale': locale, 'project': project, 'pending_variants': len(items),
                   'request_count': len(planned), 'payload_characters': characters,
                   'estimated_uncached_usd': str(estimate), 'reserved_usd': str(budget.cost),
                   'ceiling_usd': str(budget.ceiling), 'dry_run': not execute}
        if not execute:
            return summary
        run_identity = {'source_sha256': json.loads((output / 'manifest.json').read_text())['source_sha256'],
                        'locale': locale, 'project': project, 'pilot': pilot, 'surface': surface,
                        'entries': [{'id': item['id'], 'category': item['category'],
                                     'source': item['source']} for item in items]}
        atomic_json(output / 'google-runs' / (digest(run_identity) + '.json'), run_identity)
        write_targets(paths, targets, expected_hashes)
        all_failures = []
        for group in planned:
            payload = request_payload(group, locale)
            response = fetch(project, payload, output / 'google-responses', budget, translate)
            translations = response.get('translations', [])
            if len(translations) != len(payload['contents']):
                failures = [{'id': item['id'], 'category': item['category'],
                             'reason': 'Google returned wrong translation count'} for item in group]
                atomic_json(output / 'google-failures' / (digest(payload) + '.json'),
                            {'payload_sha256': digest(payload), 'failures': failures})
                all_failures.extend(failures)
                continue
            translated = iter(translations)
            restored, failures = [], []
            for item in group:
                values = iter([next(translated) for piece in item['pieces'] if isinstance(piece, dict)])
                try:
                    parts = []
                    for piece in item['pieces']:
                        if isinstance(piece, str):
                            parts.append(piece)
                        else:
                            parts.append(unmask(next(values)['translatedText'], piece['protected'], piece['source']))
                    restored.append(unmask(''.join(parts), [], item['source']))
                except (ValueError, KeyError, TypeError) as exc:
                    restored.append(None)
                    failures.append({'id': item['id'], 'category': item['category'], 'reason': str(exc)})
            for item, value in zip(group, restored):
                if value is None:
                    continue
                if item['category'] is None:
                    targets[item['surface']][item['key']] = value
                else:
                    targets[item['surface']][item['key']][item['category']] = value
            write_targets(paths, targets, expected_hashes)
            if failures:
                atomic_json(output / 'google-failures' / (digest(payload) + '.json'),
                            {'payload_sha256': digest(payload), 'failures': failures})
                all_failures.extend(failures)
        summary['reserved_usd'] = str(budget.cost)
        summary['rejected_entries'] = all_failures
        return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--locale', required=True, choices=PROGRAM.LOCALES)
    parser.add_argument('--project', required=True)
    parser.add_argument('--budget-usd', default='9')
    parser.add_argument('--limit', type=int, help='maximum blank variants (e.g. 100 for the pilot)')
    parser.add_argument('--surface', choices=('native', 'online'), help='draft one surface first using the same budget')
    parser.add_argument('--pilot', action='store_true', help='draft 100 variants distributed across source domains')
    parser.add_argument('--run', action='store_true', help='send paid requests; default only estimates')
    args = parser.parse_args()
    try:
        summary = run(args.output, args.locale, args.project, args.budget_usd,
                      args.limit, args.run, pilot=args.pilot, surface=args.surface)
        print(json.dumps(summary, indent=2))
        return int(bool(summary.get('rejected_entries')))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
