#!/usr/bin/env python3
"""Budget-limited Google NMT drafts for an existing translation-program kit.

No credentials are persisted. API results are comparison drafts, never approvals.
Plain mode fills only blank draft entries. HTML mode writes separate comparison
catalogs. Existing paid cache payloads and both ledger formats remain unchanged.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
from datetime import datetime, timezone
try:
    import fcntl
except ImportError:  # Windows: preserve the incoming portable tool entry point.
    import msvcrt
from html import escape
from html.parser import HTMLParser
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import uuid
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
    with (folder / '.lock').open('a+b') as stream:
        if os.name == 'nt':
            stream.seek(0, os.SEEK_END)
            if not stream.tell():
                stream.write(b'0')
                stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


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


def assert_backend_budget(output, mode):
    """Do not silently reset accounting by switching paid cache formats."""
    if any(p.stat().st_size for p in (output / 'google').glob('*/attempts.jsonl')):
        raise ValueError('This kit has a paid editorial ledger for the other protection mode; retain --mode editorial. Ledgers are never merged or reset.')
    segmented_ledger = output / 'google-budget.json'
    if segmented_ledger.exists() and json.loads(segmented_ledger.read_text()).get('attempts'):
        raise ValueError('This kit has a paid ledger for the other protection mode; retain the original segmented backend. Ledgers are never merged or reset.')
    plain_ledger = output / 'google-nmt' / 'ledger.json'
    plain_spent = plain_ledger.exists() and bool(json.loads(plain_ledger.read_text())['attempts'])
    html_spent = any((output / 'google').glob('*/attempts/*.json'))
    if (mode == 'html' and plain_spent) or (mode == 'plain' and html_spent):
        raise ValueError('This kit has a paid ledger for the other protection mode; '
                         'retain its original mode and budget accounting. Ledgers are never merged or reset.')


def translate(folder, entries, project, locale, limit=MAX_CHARACTERS, dispatch=request_api, execute=False):
    if folder.name == 'google-nmt':
        assert_backend_budget(folder.parent, 'plain')
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



# HTML drafts retain the Norwegian batch workflow and its existing paid cache.
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


def protect_html(text):
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


def restore_html(source, translated):
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
    manifest = json.loads((output / 'manifest.json').read_text())
    if locale not in manifest['locales']:
        raise ValueError('Locale is not in this translation kit')
    if selected and set(selected) - {batch['id'] for batch in manifest['batches']}:
        raise ValueError('Unknown batch selection')
    entries = []
    for batch in manifest['batches']:
        if selected and batch['id'] not in selected:
            continue
        payload = json.loads((output / batch['path']).read_text())
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
        encoded = protect_html(text)
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


def cache_key_html(payload, project, location):
    return fingerprint({'project': project, 'location': location, 'payload': payload})


def validate_response(chunk, response):
    values = response.get('translations', [])
    if len(values) != len(chunk):
        raise ValueError('Google returned an unexpected translation count')
    return {source: restore_html(source, value['translatedText']) for source, value in zip(chunk, values)}


def translate_html(plan, output, client, target, max_characters, retries=2, retry_invalid=False):
    assert_backend_budget(output, 'html')
    folder = output / 'google' / plan['locale']
    cache, attempts = folder / 'responses', folder / 'attempts'
    cache.mkdir(parents=True, exist_ok=True)
    attempts.mkdir(parents=True, exist_ok=True)
    translated, requests_made = {}, 0
    for chunk in plan['chunks']:
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': target,
                   'mimeType': 'text/html', 'contents': [protect_html(text) for text in chunk]}
        key = cache_key_html(payload, client.project, client.location)
        path = cache / (key + '.json')
        if path.exists():
            record = json.loads(path.read_text())
            if record.get('request') != payload or record.get('project') != client.project or record.get('location') != client.location:
                raise ValueError('Cached request provenance mismatch')
            response = record['response']
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
                charged = sum(json.loads(p.read_text())['characters'] for p in attempts.glob('*.json'))
                characters = sum(map(len, payload['contents']))
                if charged + characters > max_characters:
                    raise ValueError(f'Character budget exhausted: {charged} attempted, next request {characters}, limit {max_characters}')
                # Count before dispatch: a lost response can still have incurred a charge.
                PROGRAM.write_json(attempts / (uuid.uuid4().hex + '.json'), {
                    'request_sha256': key, 'characters': characters, 'project': client.project,
                    'location': client.location, 'attempt': attempt + 1})
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
            # Persist actual output before validation so failed translations remain inspectable.
            record = {'request': payload, 'project': client.project, 'location': client.location, 'response': response}
            with path.open('x', encoding='utf-8') as stream:
                json.dump(record, stream, ensure_ascii=False, indent=2)
                stream.write('\n')
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
        PROGRAM.write_json(run / (surface + '.json'), catalog)
    PROGRAM.write_json(run / 'metadata.json', {
        'locale': plan['locale'], 'provider': 'google-cloud-translation-v3', 'target': target,
        'project': client.project, 'location': client.location, 'source_sha256': plan['source_sha256'],
        'plan_sha256': fingerprint(plan), 'requests_made': requests_made,
        'certification': 'Machine draft; no linguistic review or locale registration'})
    return run



def run(*args, **kwargs):
    """Compatibility entry for established editorial/segmented Python callers."""
    spec = importlib.util.spec_from_file_location('google_draft_segmented', Path(__file__).with_name('google_draft_segmented.py'))
    backend = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(backend)
    return backend.run(*args, **kwargs)


def main():
    # Keep paid campaign formats explicit: the segmented upstream workflow has
    # its own shared ledger/cache and never migrates an existing Tamil campaign.
    import sys
    segmented_flags = ('--run', '--pilot', '--limit', '--surface', '--budget-usd')
    arguments = sys.argv[1:]
    mode = next((value.split('=', 1)[1] for value in arguments if value.startswith('--mode=')), None)
    if '--mode' in arguments:
        mode = arguments[arguments.index('--mode') + 1]
    if mode == 'thai':
        spec = importlib.util.spec_from_file_location('google_draft_thai', Path(__file__).with_name('google_draft_thai.py'))
        backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(backend)
        sys.argv = [sys.argv[0], *[value for value in arguments if value != '--mode=thai']]
        if '--mode' in sys.argv:
            position = sys.argv.index('--mode')
            del sys.argv[position:position + 2]
        return backend.main()
    legacy_editorial_flags = ('--max-usd', '--batch-characters')
    if mode in ('plan', 'blank-fill', 'editorial') or any(value.split('=', 1)[0] in segmented_flags + legacy_editorial_flags for value in arguments):
        spec = importlib.util.spec_from_file_location('google_draft_segmented', Path(__file__).with_name('google_draft_segmented.py'))
        backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(backend)
        return backend.main()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing translation-program kit')
    parser.add_argument('--locale', default='nb', choices=PROGRAM.LOCALES)
    parser.add_argument('--project')
    parser.add_argument('--location', default='global')
    parser.add_argument('--mode', choices=('plain', 'html'),
                        help='Protection/cache backend; defaults to HTML for nb or --batches, plain otherwise')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--keys', type=Path, help='Plain-mode pilot JSON array of native:/online: IDs')
    selection.add_argument('--batches', nargs='+', help='HTML-mode pilot batch IDs')
    parser.add_argument('--request-characters', type=int, default=4000, help='HTML request payload character limit')
    parser.add_argument('--max-characters', type=int, help='Cumulative attempted budget; plain defaults to400000 / US$8')
    parser.add_argument('--price-per-million', type=float, default=20, help='HTML planning estimate only')
    parser.add_argument('--retry-invalid-responses', action='store_true', help='HTML only: retain rejected output before a budgeted retry')
    parser.add_argument('--execute', action='store_true', help='Make billable requests; default only estimates')
    args = parser.parse_args()
    mode = args.mode or ('html' if args.locale == 'nb' or args.batches else 'plain')
    try:
        if mode == 'plain':
            if args.batches or args.retry_invalid_responses or args.location != 'global':
                raise ValueError('Batch selection, invalid-response retry and regional locations require --mode html')
            if not args.project:
                raise ValueError('Plain-mode planning requires an explicit --project for its shared budget ledger')
            folder = args.output / 'google-nmt'
            with locked(args.output / 'google-draft-control'), locked(folder):
                assert_backend_budget(args.output, 'plain')
                manifest, source, targets, paths, invariants = load_kit(args.output, args.locale)
                selected = json.loads(args.keys.read_text()) if args.keys else None
                if selected is not None:
                    all_keys = {surface + ':' + key for surface, catalog in source.items() for key in catalog}
                    if not isinstance(selected, list) or set(selected) - all_keys:
                        raise ValueError('Pilot contains unknown entry IDs')
                entries = pending_entries(source, targets, invariants, selected)
                limit = args.max_characters if args.max_characters is not None else MAX_CHARACTERS
                summary, results = translate(folder, entries, args.project, args.locale, limit, execute=args.execute)
                if args.execute:
                    apply_results(paths, entries, results, args.project, args.locale)
                print(json.dumps(summary, indent=2))
        else:
            if args.keys:
                raise ValueError('--keys requires --mode plain; HTML pilots use --batches')
            if not 1 <= args.request_characters <= 30000 or args.price_per_million < 0:
                raise ValueError('Invalid request size or price')
            plan = make_plan(args.output, args.locale, args.batches, args.request_characters)
            characters = sum(len(protect_html(text)) for chunk in plan['chunks'] for text in chunk)
            print(json.dumps({'entries': len(plan['entries']), 'requests_before_cache': len(plan['chunks']),
                              'characters_before_cache': characters,
                              'estimated_cost_before_cache': round(characters * args.price_per_million / 1000000, 4)}), flush=True)
            if args.execute:
                if not args.project or not args.max_characters or args.max_characters < 1:
                    raise ValueError('--execute requires --project and a positive --max-characters')
                client = GoogleClient(args.project, args.location)
                target = client.target(args.locale)
                print('Provider target: ' + target, flush=True)
                with locked(args.output / 'google-draft-control'), locked(args.output / 'google' / args.locale):
                    print(translate_html(plan, args.output, client, target, args.max_characters,
                                         retry_invalid=args.retry_invalid_responses), flush=True)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        if isinstance(error, HTTPError):
            parser.exit(1, f'Google Translation HTTP {error.code}; response body omitted\n')
        parser.exit(1, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
