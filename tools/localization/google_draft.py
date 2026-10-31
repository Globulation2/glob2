#!/usr/bin/env python3
"""Resumable Google NMT drafts for a frozen translation_program kit.

Uses gcloud's existing login, never saves its token, and never creates review
sign-offs. All generated evidence stays inside the supplied kit directory.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
from html import escape
from html.parser import HTMLParser
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
TOKEN = re.compile(r'(?<![A-Za-z0-9])/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+/?|(?:Ctrl|Alt|Shift|Cmd)(?:\s*\+\s*[A-Za-z0-9]+)*|%[0-9]+|\{[a-zA-Z][a-zA-Z0-9_]*\}|\\[nrt\\]|https?://[^\s<>]+|[\n\r\t]')
COMMON_INVARIANTS = ('Globulation 2', 'Hive Mind', 'JavaScript', 'CC-BY-SA-4.0',
                     'CC-BY-4.0', 'CC0-1.0', 'UTF-8', 'SHA-256', '.opus',
                     'Castor', 'Cortex', 'Cabino', 'Econo', 'Maxima', 'Nicowar', 'Numbi', 'Warrush')
RATE = 20 / 1_000_000


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


@contextmanager
def exclusive(path):
    """Reject concurrent writers; a crashed runner leaves a visible lock to inspect."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        os.write(fd, str(os.getpid()).encode())
        os.close(fd)
        yield
    finally:
        path.unlink()


def protect(text, invariants=()):
    pieces = [re.escape(term) for term in invariants if term]
    pattern = re.compile('|'.join(pieces + [TOKEN.pattern]))
    result, end = [], 0
    for match in pattern.finditer(text):
        result.extend((escape(text[end:match.start()]),
                       '<span translate="no" class="notranslate">' + escape(match.group()) + '</span>'))
        end = match.end()
    result.append(escape(text[end:]))
    return ''.join(result)


class TextOnly(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_data(self, data):
        self.parts.append(data)


def restore(original, translated, invariants=()):
    if not original.strip():
        return original
    parser = TextOnly()
    parser.feed(translated)
    parser.close()
    value = ''.join(parser.parts).strip()
    leading = re.match(r'^\s*', original).group()
    trailing = re.search(r'\s*$', original).group()
    value = leading + value + trailing
    if Counter(TOKEN.findall(original)) != Counter(TOKEN.findall(value)):
        raise ValueError('Translation damaged placeholders, escapes, URLs or line breaks')
    if any(value.count(term) != original.count(term) for term in invariants):
        raise ValueError('Translation damaged an invariant')
    if '\ufffd' in value or '\x00' in value or (original.strip() and not value.strip()):
        raise ValueError('Translation is empty or contains invalid characters')
    return value


def entries(native, online, categories, invariants):
    for key, text in native.items():
        yield ('native', key, None, text, tuple(term for term in COMMON_INVARIANTS if term in text))
    for key, value in online.items():
        shared = tuple(term for term in COMMON_INVARIANTS if term in key)
        protected = tuple(dict.fromkeys(tuple(invariants.get(key, ())) + shared))
        if isinstance(value, str):
            yield ('online', key, None, value, protected)
        else:
            for category in categories:
                yield ('online', key, category, value.get(category, value['other']), protected)


def request_payload(project, locale, contents):
    return {'sourceLanguageCode': 'en', 'targetLanguageCode': locale,
            'mimeType': 'text/html', 'contents': contents,
            'model': f'projects/{project}/locations/global/models/general/nmt'}


def request_google(project, locale, contents):
    token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True,
                                    stderr=subprocess.DEVNULL).strip()
    url = f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText'
    request = urllib.request.Request(url, json.dumps(request_payload(project, locale, contents)).encode(), headers={
        'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json',
        'x-goog-user-project': project})
    with urllib.request.urlopen(request, timeout=90) as response:
        return json.load(response)


def append_attempt(path, attempt):
    with path.open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(attempt, ensure_ascii=False) + '\n')
        stream.flush()
        os.fsync(stream.fileno())


def run(output, locale='fil', project='bradley-personal-20261027', limit=None,
        max_usd=10, batch_characters=12000, root=PROGRAM.ROOT, send=request_google):
    if not math.isfinite(max_usd) or max_usd <= 0 or max_usd > 12 or not 1 <= batch_characters <= 25000:
        raise ValueError('Budget must be finite, positive and at most US$12; batch size must be 1..25000 characters')
    if limit is not None and limit < 1:
        raise ValueError('Pilot limit must be positive')
    manifest = json.loads((output / 'manifest.json').read_text())
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in this kit')
    _, native, online, _, source_hash = PROGRAM.sources(root)
    if source_hash != manifest['source_sha256']:
        raise ValueError('Source changed; prepare a new kit')
    for surface, catalog in [('native', native), ('online', online)]:
        if json.loads((output / 'source' / (surface + '.json')).read_text()) != catalog:
            raise ValueError('Frozen source snapshot changed')
    invariants = json.loads((root / 'platform/packages/i18n/invariants.json').read_text())
    paths = PROGRAM.catalog_paths(output, locale)
    target_native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    target_online = json.loads(paths['online'].read_text())
    state = output / 'google' / locale
    with exclusive(state / 'runner.lock'):
        config = {'version': 1, 'source_sha256': source_hash, 'project': project, 'locale': locale,
                  'model': 'general/nmt', 'rate_usd_per_million': 20}
        config_path = state / 'config.json'
        if config_path.exists() and json.loads(config_path.read_text()) != config:
            raise ValueError('Google draft configuration changed; use a new kit')
        atomic_json(config_path, config)
        ledger = state / 'attempts.jsonl'
        spent_characters = sum(json.loads(line)['characters'] for line in ledger.read_text().splitlines()) if ledger.exists() else 0
        cache_path = state / 'cache.json'
        cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
        if locale == 'fil':
            for key, value in {'[language-code]': 'fil', '[language]': 'Filipino',
                               '[language incomplete]': 'Filipino — Hindi kumpleto'}.items():
                if key in native and not target_native.get(key):
                    target_native[key] = value
        pending = {}
        for surface, key, category, text, protected in entries(native, online, PROGRAM.plural_categories(locale), invariants):
            target = target_native if surface == 'native' else target_online
            existing = target.get(key, '')
            existing = existing.get(category, '') if category is not None and isinstance(existing, dict) else existing
            if existing:
                continue
            if not text.strip():
                continue
            html = protect(text, protected)
            identity = PROGRAM.digest(html.encode())
            prose = TOKEN.sub('', text)
            for term in protected:
                prose = prose.replace(term, '')
            if not any(character.isalpha() for character in prose):
                cache.setdefault(identity, html)
            pending.setdefault(identity, {'html': html, 'entries': []})['entries'].append((surface, key, category, text, protected))
        selected = list(pending)
        if limit:
            # Round-robin across source batches, with domains/surfaces first,
            # so short pilots do not consist solely of native alphabetic labels.
            references = {}
            for identity, item in pending.items():
                for surface, key, *_ in item['entries']:
                    references.setdefault(surface + ':' + key, []).append(identity)
            groups = {}
            for batch in manifest['batches']:
                surface, domain, _ = batch['id'].split('-', 2)
                group = groups.setdefault((surface, domain), [])
                candidates = list(dict.fromkeys(identity for key in batch['keys']
                                                for identity in references.get(key, [])))
                if candidates:
                    group.append(candidates)
            selected, seen = [], set()
            while groups and len(selected) < limit:
                for group_id in list(groups):
                    batches = groups[group_id]
                    candidates = batches.pop(0)
                    while candidates and candidates[0] in seen:
                        candidates.pop(0)
                    if candidates and len(selected) < limit:
                        identity = candidates.pop(0)
                        selected.append(identity)
                        seen.add(identity)
                    if candidates:
                        batches.append(candidates)
                    if not batches:
                        del groups[group_id]
        failures = []

        def apply_cached():
            for identity in selected:
                if identity not in cache:
                    continue
                for surface, key, category, text, protected in pending[identity]['entries']:
                    try:
                        value = restore(text, cache[identity], protected)
                    except ValueError as exc:
                        failures.append({'surface': surface, 'key': key, 'category': category, 'reason': str(exc)})
                        continue
                    target = target_native if surface == 'native' else target_online
                    if category is None:
                        target[key] = value
                    else:
                        target.setdefault(key, {})[category] = value
            # Save after every response; never replace any nonblank starting value.
            temporary = paths['native'].with_suffix('.tmp')
            temporary.write_text(''.join(key + '\n' + target_native.get(key, '') + '\n' for key in native), encoding='utf-8')
            temporary.replace(paths['native'])
            atomic_json(paths['online'], target_online)

        missing = [identity for identity in selected if identity not in cache]
        apply_cached()
        while missing:
            batch, characters = [], 0
            while missing and len(batch) < 100:
                identity = missing[0]
                size = len(pending[identity]['html'])
                if size > 25000:
                    raise ValueError('Single entry exceeds request size; split it with explicit review')
                if batch and characters + size > batch_characters:
                    break
                batch.append(missing.pop(0))
                characters += size
            if (spent_characters + characters) * RATE > max_usd:
                raise ValueError('Conservative API budget exhausted; cached drafts are retained')
            attempt_id = str(time.time_ns())
            contents = [pending[identity]['html'] for identity in batch]
            atomic_json(state / 'requests' / (attempt_id + '.json'), request_payload(project, locale, contents))
            append_attempt(ledger, {'id': attempt_id, 'characters': characters,
                                   'runner_sha256': PROGRAM.digest(Path(__file__).read_bytes()),
                                   'estimated_usd': characters * RATE, 'contents_sha256': batch,
                                   'status': 'reserved-before-request'})
            spent_characters += characters
            response = send(project, locale, contents)
            atomic_json(state / 'responses' / (attempt_id + '.json'), response)
            translations = response.get('translations', [])
            if len(translations) != len(batch) or any(not isinstance(item.get('translatedText'), str) for item in translations):
                raise ValueError('Invalid Google response; attempt counted and raw response retained')
            cache.update({identity: item['translatedText'] for identity, item in zip(batch, translations)})
            atomic_json(cache_path, cache)
            failures = []
            apply_cached()
        atomic_json(state / 'rejected.json', failures)
        summary = {'selected_unique_requests': len(selected), 'rejected_entries': len(failures),
                   'attempted_characters_including_failures': spent_characters,
                   'estimated_usd_before_credits': round(spent_characters * RATE, 6)}
        atomic_json(state / 'summary.json', summary)
        return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--locale', default='fil', choices=PROGRAM.LOCALES)
    parser.add_argument('--project', default='bradley-personal-20261027')
    parser.add_argument('--pilot', type=int, help='translate at most this many distinct pending texts')
    parser.add_argument('--max-usd', type=float, default=10, help='conservative cumulative ceiling (at most 12)')
    parser.add_argument('--batch-characters', type=int, default=12000)
    args = parser.parse_args()
    if args.pilot is not None and args.pilot < 1:
        parser.error('--pilot must be positive')
    try:
        print(json.dumps(run(args.output, args.locale, args.project, args.pilot, args.max_usd, args.batch_characters)))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')


if __name__ == '__main__':
    main()
