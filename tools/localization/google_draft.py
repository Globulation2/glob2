#!/usr/bin/env python3
"""Budgeted Google NMT drafts for a frozen translation_program kit.

Dry-run is the default. --execute writes separate HTML-plan machine outputs;
--mode blank-fill --run fills only blank draft entries using segmented text/plain. It never registers languages, overwrites editorial work or signs reviews.
All response/cache/budget evidence stays inside the kit's ignored directory.
"""
import argparse
from html import escape
from html.parser import HTMLParser
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import uuid
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
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in the frozen kit')
    invariants_path = PROGRAM.ROOT / 'platform/packages/i18n/invariants.json'
    if PROGRAM.digest(invariants_path.read_bytes()) != manifest['source_files']['platform/packages/i18n/invariants.json']:
        raise ValueError('Invariant contract changed since the kit was frozen')
    invariants = json.loads(invariants_path.read_text(encoding='utf-8'))
    paths = PROGRAM.catalog_paths(output, locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    online = json.loads(paths['online'].read_text(encoding='utf-8'))
    targets = {'native': native, 'online': online}
    result = []
    plural_categories = PROGRAM.plural_categories(locale)
    for batch in manifest['batches']:
        payload = json.loads((output / batch['path']).read_text(encoding='utf-8'))
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
        if not self.ceiling.is_finite() or not Decimal('0') < self.ceiling <= MAX_BUDGET_USD:
            raise ValueError('Budget must be greater than zero and at most US$9')
        self.value = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {
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

    def _reload(self):
        if self.path.exists():
            value = json.loads(self.path.read_text(encoding='utf-8'))
            if value['project'] != self.project or value['rate_usd_per_million'] != str(RATE_USD_PER_MILLION):
                raise ValueError('Budget project/rate differs from existing ledger')
            self.value = value

    @property
    def lock_path(self):
        return self.path.with_name(self.path.name + '.lock')

    def reserve(self, payload, cache_key):
        # Low-level fetch users share this lock too, even outside run()/translate().
        with file_lock(self.lock_path):
            self._reload()
            characters = sum(len(text) for text in payload['contents'])
            projected = self.cost + Decimal(characters) * RATE_USD_PER_MILLION / Decimal(1000000)
            if projected > self.ceiling:
                raise ValueError(f'Google request would exceed US${self.ceiling} budget; no request sent')
            self.value['attempts'].append({'id': len(self.value['attempts']) + 1, 'characters': characters,
                                          'request_sha256': cache_key, 'state': 'dispatched_or_uncertain'})
            atomic_json(self.path, self.value)
            return self.value['attempts'][-1]

    def complete(self, attempt, response):
        with file_lock(self.lock_path):
            self._reload()
            saved = next(a for a in self.value['attempts'] if a['id'] == attempt['id'])
            if saved['request_sha256'] != attempt['request_sha256']:
                raise ValueError('Budget reservation identity changed')
            saved.update(state='response_received', response_sha256=digest(response))
            atomic_json(self.path, self.value)


@contextmanager
def file_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as lock:
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


@contextmanager
def kit_lock(output):
    with file_lock(output / '.google-draft.lock'):
        yield


_access_token = None
_access_token_time = 0.0


def google_translate(project, payload):
    # A subprocess argv rather than shell expansion keeps the token out of logs.
    global _access_token, _access_token_time
    now = time.monotonic()
    if _access_token is None or now - _access_token_time > 2400:
        _access_token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True,
                                                  stderr=subprocess.DEVNULL, timeout=60).strip()
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
        cached = json.loads(path.read_text(encoding='utf-8'))
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
        _import_plan_attempts(output, budget)
        planned = list(batches(items))
        characters = sum(len(text) for item in items for text in item_contents(item))
        estimate = Decimal(characters) * RATE_USD_PER_MILLION / Decimal(1000000)
        summary = {'locale': locale, 'project': project, 'pending_variants': len(items),
                   'request_count': len(planned), 'payload_characters': characters,
                   'estimated_uncached_usd': str(estimate), 'reserved_usd': str(budget.cost),
                   'ceiling_usd': str(budget.ceiling), 'dry_run': not execute}
        if not execute:
            return summary
        run_identity = {'source_sha256': json.loads((output / 'manifest.json').read_text(encoding='utf-8'))['source_sha256'],
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



# HTML-plan API retained for existing translation kits and paid response caches.
# Standalone routes are executable links too. Exclude slashes within ordinary
# words/MIME types and within absolute URLs, which have their own protection.
LINK = re.compile(r'(?<![\w/])www\.[^\s<>]+|(?<![\w:/])/[A-Za-z0-9._~/-]+(?:[?#][^\s<>]*)?')
# Keep executable substitutions, authored markup, URLs and literal line breaks intact.
PROTECTED = re.compile(r'https?://[^\s<>]+|<[^>]+>|\{[a-zA-Z][a-zA-Z0-9_]*\}|%[0-9]+|%%|\\[nrt]|' + LINK.pattern)
# Validate bare schemes independently: Norwegian can attach a compound suffix,
# as in "https://-adresse", without creating a URL. Keep outbound protection
# stable so already-paid exact request payloads remain resumable.
URL = re.compile(r'https?://[a-zA-Z0-9\[][\S]*?(?=[\s<>]|$)')
SCHEME = re.compile(r'https?://')
CONTENT_TOKEN = re.compile(r'<[^>]+>|\{[a-zA-Z][a-zA-Z0-9_]*\}|%[0-9]+|%%|\\[nrt]')


def protected_tokens(text):
    return Counter(CONTENT_TOKEN.findall(text) + URL.findall(text) + SCHEME.findall(text) + LINK.findall(text))



def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def _require_html_single_line(text):
    if re.search(r'[\r\n\t]', text):
        raise ValueError('Physical line breaks/tabs require --mode blank-fill')


def protect(text):
    _require_html_single_line(text)
    parts, position = [], 0
    for token in PROTECTED.finditer(text):
        parts.append(escape(text[position:token.start()]))
        parts.append('<span translate="no" class="notranslate">' + escape(token.group()) + '</span>')
        position = token.end()
    parts.append(escape(text[position:]))
    return ''.join(parts)


class PlainText(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_data(self, data):
        self.parts.append(data)


def restore(source, translated):
    _require_html_single_line(source)
    parser = PlainText()
    parser.feed(translated)
    parser.close()
    result = ''.join(parser.parts)
    # Translation services can normalize outer spaces, which are significant in fragments.
    prefix = source[:len(source) - len(source.lstrip())]
    suffix = source[len(source.rstrip()):]
    result = prefix + result.strip() + suffix if source.strip() else source
    if protected_tokens(source) != protected_tokens(result):
        raise ValueError('Translation changed protected tokens')
    if not result.strip() and source.strip():
        raise ValueError('Translation returned empty text')
    if '\ufffd' in result or '\x00' in result or '\n' in result or '\r' in result:
        raise ValueError('Translation contains invalid or physical line-break characters')
    return result


def make_plan(output, locale, selected=None, request_characters=4000):
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in this translation kit')
    if selected and set(selected) - {batch['id'] for batch in manifest['batches']}:
        raise ValueError('Unknown batch selection')
    entries = []
    for batch in manifest['batches']:
        if selected and batch['id'] not in selected:
            continue
        payload = json.loads((output / batch['path']).read_text(encoding='utf-8'))
        if payload['source_sha256'] != manifest['source_sha256']:
            raise ValueError('Batch source hash does not match manifest')
        for entry in payload['entries']:
            variants = {'': entry['source']} if isinstance(entry['source'], str) else entry['source']
            for variant, text in variants.items():
                entries.append({'surface': payload['surface'], 'key': entry['key'], 'variant': variant,
                                'source': text, 'batch': batch['id']})
    # Deduplicate identical source text, preserving stable source order and all key mappings.
    texts = list(dict.fromkeys(item['source'] for item in entries if item['source'].strip()))
    chunks, chunk, size = [], [], 0
    for text in texts:
        encoded = protect(text)
        if len(encoded) > 30000:
            raise ValueError('A single source exceeds the API request limit')
        if chunk and (size + len(encoded) > request_characters or len(chunk) >= 100):
            chunks.append(chunk)
            chunk, size = [], 0
        chunk.append(text)
        size += len(encoded)
    if chunk:
        chunks.append(chunk)
    return {'source_sha256': manifest['source_sha256'], 'locale': locale,
            'entries': entries, 'chunks': chunks}


class GoogleClient:
    def __init__(self, project, location='global'):
        if not re.fullmatch(r'[a-zA-Z0-9:._-]+', project) or not re.fullmatch(r'[a-z0-9-]+', location):
            raise ValueError('Invalid Google project or location')
        self.project, self.location = project, location
        self._token, self._token_time = None, 0

    def token(self, refresh=False):
        if refresh or self._token is None or time.monotonic() - self._token_time > 2700:
            self._token = subprocess.check_output(
                ['gcloud', 'auth', 'print-access-token'], text=True,
                stderr=subprocess.DEVNULL, timeout=60).strip()
            if not self._token:
                raise ValueError('gcloud returned an empty access token')
            self._token_time = time.monotonic()
        return self._token

    def request(self, request):
        for attempt in range(2):
            request.headers['Authorization'] = 'Bearer ' + self.token(refresh=attempt == 1)
            try:
                callback = getattr(self, '_before_dispatch', None)
                if callback is not None and request.get_method() == 'POST':
                    callback()
                with urlopen(request, timeout=90) as response:
                    return json.load(response)
            except HTTPError as error:
                if error.code != 401 or attempt:
                    raise
                error.close()

    def call(self, method, payload):
        request = Request(f'https://translation.googleapis.com/v3/projects/{self.project}/locations/{self.location}:{method}',
                          data=json.dumps(payload).encode(), method='POST',
                          headers={'Content-Type': 'application/json',
                                   'x-goog-user-project': self.project})
        return self.request(request)

    def target(self, locale):
        # supportedLanguages uses GET, unlike translateText.
        request = Request(f'https://translation.googleapis.com/v3/projects/{self.project}/locations/{self.location}/supportedLanguages',
                          headers={'x-goog-user-project': self.project})
        languages = {item['languageCode'] for item in self.request(request)['languages'] if item.get('supportTarget')}
        candidates = ['nb', 'no'] if locale == 'nb' else [locale]
        for candidate in candidates:
            if candidate in languages:
                return candidate
        raise ValueError('Requested target language is unavailable')


def cache_key(payload, project, location):
    return fingerprint({'project': project, 'location': location, 'payload': payload})


def validate_response(chunk, response):
    values = response.get('translations', [])
    if len(values) != len(chunk):
        raise ValueError('Google returned an unexpected translation count')
    return {source: restore(source, value['translatedText']) for source, value in zip(chunk, values)}


def _translate_plan(plan, output, client, target, max_characters, retries, retry_invalid, budget):
    if max_characters < 1 or retries < 0:
        raise ValueError('Character budget and retry count must be positive/nonnegative')
    folder = output / 'google' / plan['locale']
    cache, attempts = folder / 'responses', folder / 'attempts'
    cache.mkdir(parents=True, exist_ok=True)
    attempts.mkdir(parents=True, exist_ok=True)
    translated, requests_made = {}, 0
    for chunk in plan['chunks']:
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': target,
                   'mimeType': 'text/html', 'contents': [protect(text) for text in chunk]}
        key = cache_key(payload, client.project, client.location)
        path = cache / (key + '.json')
        if path.exists():
            record = json.loads(path.read_text(encoding='utf-8'))
            if record.get('request') != payload or record.get('project') != client.project or record.get('location') != client.location:
                raise ValueError('Cached request provenance mismatch')
            response = record['response']
            if 'response_sha256' in record and record['response_sha256'] != digest(response):
                raise ValueError('Corrupt Google response cache')
            try:
                validate_response(chunk, response)
            except (ValueError, KeyError, TypeError):
                if not retry_invalid:
                    raise ValueError('Cached invalid response retained; use --retry-invalid-responses to retry within budget') from None
                rejected = folder / 'rejected'
                rejected.mkdir(exist_ok=True)
                path.rename(rejected / (key + '-' + uuid.uuid4().hex + '.json'))
        if not path.exists():
            for attempt in range(retries + 1):
                reservations = []
                def reserve_dispatch():
                    charged = sum(json.loads(p.read_text(encoding='utf-8'))['characters']
                                  for p in attempts.glob('*.json'))
                    characters = sum(map(len, payload['contents']))
                    if charged + characters > max_characters:
                        raise ValueError(f'Character budget exhausted: {charged} attempted, next request {characters}, limit {max_characters}')
                    reservation = budget.reserve(payload, key)
                    reservations.append(reservation)
                    atomic_json(attempts / (uuid.uuid4().hex + '.json'), {
                        'request_sha256': key, 'characters': characters, 'project': client.project,
                        'location': client.location, 'attempt': attempt + 1,
                        'campaign_attempt_id': reservation['id']})
                # Real clients invoke this immediately before each HTTP POST,
                # including an authentication refresh. Test/adapter clients reserve
                # once before their public call. Both APIs share the same kit lock.
                real_client = isinstance(client, GoogleClient)
                if real_client:
                    client._before_dispatch = reserve_dispatch
                else:
                    reserve_dispatch()
                try:
                    response = client.call('translateText', payload)
                    requests_made += 1
                    break
                except (HTTPError, URLError, TimeoutError) as error:
                    code = error.code if isinstance(error, HTTPError) else None
                    if isinstance(error, HTTPError):
                        error.close()
                    if code is not None and code not in (429, 500, 502, 503, 504):
                        raise ValueError(f'Google Translation HTTP {code}; response body omitted') from None
                    if attempt == retries:
                        raise ValueError('Google Translation retries exhausted; attempts retained for budgeting') from None
                    time.sleep(min(2 ** attempt, 8))
                finally:
                    if real_client:
                        client._before_dispatch = None
            # Persist actual output before validation so failed translations remain inspectable.
            record = {'request': payload, 'project': client.project, 'location': client.location,
                      'response': response, 'response_sha256': digest(response)}
            atomic_json(path, record)
            budget.complete(reservations[-1], response)
        translated.update(validate_response(chunk, response))
    catalogs = {'native': {}, 'online': {}}
    for item in plan['entries']:
        value = translated.get(item['source'], item['source'])
        catalog = catalogs[item['surface']]
        if item['variant']:
            catalog.setdefault(item['key'], {})[item['variant']] = value
        else:
            catalog[item['key']] = value
    run = folder / 'runs' / fingerprint({'plan': plan, 'project': client.project, 'location': client.location, 'target': target})
    for surface, catalog in catalogs.items():
        atomic_json(run / (surface + '.json'), catalog)
    atomic_json(run / 'metadata.json', {
        'locale': plan['locale'], 'provider': 'google-cloud-translation-v3', 'target': target,
        'project': client.project, 'location': client.location, 'source_sha256': plan['source_sha256'],
        'plan_sha256': fingerprint(plan), 'requests_made': requests_made,
        'certification': 'Machine draft; no linguistic review or locale registration'})
    return run



def _import_plan_attempts(output, budget):
    """Conservatively adopt pre-merge HTML API attempts into the shared ledger.

    Original attempt files stay byte-identical. Stable path/content identities
    make adoption idempotent; attempts already tied to the campaign are skipped.
    """
    with file_lock(budget.lock_path):
        budget._reload()
        imported = {a.get('legacy_plan_attempt_sha256') for a in budget.value['attempts']}
        changed = False
        for path in sorted((output / 'google').glob('*/attempts/*.json')):
            record = json.loads(path.read_text(encoding='utf-8'))
            if record.get('project') != budget.project:
                continue
            linked = next((a for a in budget.value['attempts']
                           if a['id'] == record.get('campaign_attempt_id')), None)
            if linked and linked['request_sha256'] == record['request_sha256'] and linked['characters'] == record['characters']:
                continue
            identity = digest({'path': str(path.relative_to(output)), 'record': record})
            if identity in imported:
                continue
            characters = record['characters']
            if not isinstance(characters, int) or isinstance(characters, bool) or characters < 0:
                raise ValueError('Invalid historical Google attempt character count')
            budget.value['attempts'].append({
                'id': len(budget.value['attempts']) + 1, 'characters': characters,
                'request_sha256': record['request_sha256'], 'state': 'dispatched_or_uncertain',
                'legacy_plan_attempt_sha256': identity})
            imported.add(identity)
            changed = True
        if changed:
            atomic_json(budget.path, budget.value)


def translate(plan, output, client, target, max_characters, retries=2, retry_invalid=False,
              ceiling='9'):
    """HTML-plan API; writes separate machine outputs, never reviewed catalogs.

    Existing HTML cache paths/payloads remain compatible. The global USD ledger
    and exclusive writer lock are shared with the text/plain blank-fill API.
    The additional character cap retains the upstream locale-attempt contract.
    """
    with kit_lock(output):
        budget = Budget(output / 'google-budget.json', client.project, ceiling)
        _import_plan_attempts(output, budget)
        return _translate_plan(plan, output, client, target, max_characters,
                               retries, retry_invalid, budget)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing frozen translation-program kit')
    parser.add_argument('--locale', default='nb')
    parser.add_argument('--project')
    parser.add_argument('--location', default='global')
    parser.add_argument('--budget-usd', default='9', help='Shared campaign ceiling, at most US$9 at $20/million characters')
    parser.add_argument('--mode', choices=('plan', 'blank-fill'), default=None,
                        help='Default plan API; --run/pilot/limit/surface imply blank-fill')
    parser.add_argument('--batches', nargs='+', help='Plan API: selected source batch IDs')
    parser.add_argument('--request-characters', type=int, default=4000)
    parser.add_argument('--max-characters', type=int, help='Plan API: cumulative locale attempted-character limit, including retries')
    parser.add_argument('--price-per-million', type=float, default=20, help='Dry-run estimate only; enforced ledger uses $20/million')
    parser.add_argument('--retry-invalid-responses', action='store_true')
    parser.add_argument('--execute', action='store_true', help='Plan API: send billable requests and write separate machine outputs')
    parser.add_argument('--run', action='store_true', help='Blank-fill API: send billable requests and fill only blank draft variants')
    parser.add_argument('--limit', type=int)
    parser.add_argument('--surface', choices=('native', 'online'))
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    try:
        legacy = args.run or args.pilot or args.limit is not None or args.surface is not None or args.mode == 'blank-fill'
        if args.mode == 'plan' and legacy:
            raise ValueError('Blank-fill flags cannot be combined with --mode plan')
        if legacy:
            if args.execute or args.batches or args.max_characters is not None or args.retry_invalid_responses:
                raise ValueError('Plan-only flags cannot be combined with blank-fill mode')
            if not args.project:
                raise ValueError('Blank-fill mode requires --project')
            summary = run(args.output, args.locale, args.project, args.budget_usd,
                          args.limit, args.run, pilot=args.pilot, surface=args.surface)
            print(json.dumps(summary, indent=2))
            return int(bool(summary.get('rejected_entries')))
        if not 1 <= args.request_characters <= 30000 or args.price_per_million < 0:
            raise ValueError('Invalid request size or price')
        plan = make_plan(args.output, args.locale, args.batches, args.request_characters)
        characters = sum(len(protect(text)) for chunk in plan['chunks'] for text in chunk)
        print(json.dumps({'entries': len(plan['entries']), 'requests_before_cache': len(plan['chunks']),
                          'characters_before_cache': characters,
                          'estimated_cost_before_cache': round(characters * args.price_per_million / 1000000, 4)}), flush=True)
        if args.execute:
            if not args.project or not args.max_characters or args.max_characters < 1:
                raise ValueError('--execute requires --project and a positive --max-characters')
            client = GoogleClient(args.project, args.location)
            target = client.target(args.locale)
            print('Provider target: ' + target, flush=True)
            print(translate(plan, args.output, client, target, args.max_characters,
                            retry_invalid=args.retry_invalid_responses, ceiling=args.budget_usd), flush=True)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        if isinstance(error, HTTPError):
            parser.exit(1, f'Google Translation HTTP {error.code}; response body omitted\n')
        parser.exit(1, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
