#!/usr/bin/env python3
"""Create resumable Google Cloud drafts using either established kit workflow.

Filipino's default editorial workflow fills blank drafts and preserves its original
request/cache format. Other locales use the non-mutating source-plan workflow;
--execute with --max-characters also selects that workflow for Filipino. The two
response caches are deliberately separate to retain already-paid exact payloads.
"""
import argparse
from contextlib import contextmanager
import math
import os
import urllib.request
from collections import Counter
import hashlib
from html import escape
from html.parser import HTMLParser
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import uuid

SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
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


def protect(text):
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


def translate(plan, output, client, target, max_characters, retries=2, retry_invalid=False):
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


def protect_editorial(text, invariants=()):
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


def restore_editorial(original, translated, invariants=()):
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
            html = protect_editorial(text, protected)
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
                        value = restore_editorial(text, cache[identity], protected)
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
    parser.add_argument('--output', type=Path, required=True, help='Existing translation-program kit')
    parser.add_argument('--locale', default='fil')
    parser.add_argument('--pilot', type=int)
    parser.add_argument('--max-usd', type=float, default=10)
    parser.add_argument('--batch-characters', type=int, default=12000)
    parser.add_argument('--project')
    parser.add_argument('--location', default='global')
    parser.add_argument('--batches', nargs='+', help='Selected pilot batch IDs; omit for all')
    parser.add_argument('--request-characters', type=int, default=4000)
    parser.add_argument('--max-characters', type=int, help='Cumulative attempted characters, including retries and prior runs')
    parser.add_argument('--price-per-million', type=float, default=20, help='Planning estimate only; verify current project pricing')
    parser.add_argument('--retry-invalid-responses', action='store_true', help='Archive invalid cached outputs and retry them within the same budget')
    parser.add_argument('--execute', action='store_true', help='Source-plan workflow: make billable requests instead of only estimating (Filipino defaults to the editorial runner)' )
    args = parser.parse_args()
    try:
        if args.locale == 'fil' and not (args.execute or args.batches or args.max_characters):
            print(json.dumps(run(args.output, args.locale, args.project or 'bradley-personal-20261027',
                                 args.pilot, args.max_usd, args.batch_characters)))
            return 0
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
            print(translate(plan, args.output, client, target, args.max_characters, retry_invalid=args.retry_invalid_responses), flush=True)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        if isinstance(error, HTTPError):
            parser.exit(1, f'Google Translation HTTP {error.code}; response body omitted\n')
        parser.exit(1, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
