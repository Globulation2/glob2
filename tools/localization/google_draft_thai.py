#!/usr/bin/env python3
"""Resumable, budget-bounded Google v3 drafts for a frozen translation-program kit.

Only development artifacts are written. Review records and registered catalogs are
never changed. Every attempted request is reserved before transmission, including
ambiguous network failures. The local ceiling applies to this campaign, not to
other consumers of the Google Cloud billing account.
"""
import argparse
import sys
import uuid
from html import escape
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from collections import Counter
from contextlib import contextmanager
from decimal import Decimal
import hashlib
import html
from html.parser import HTMLParser
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
TOKENS = re.compile(r'%[0-9]+|\{[A-Za-z][A-Za-z0-9_]*\}')
TECHNICAL = re.compile(r'\\n|:|\[[^\]\r\n]+\]|\b(?:Ctrl|Alt|Shift|Meta|Cmd)\+(?:[A-Za-z0-9+]+)|https?://[^\s<>]+|(?<![\w])(?:/[A-Za-z0-9_.-]+){2,}|(?<![\w])(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+\.[A-Za-z0-9]+|[\w.+-]+@[\w.-]+\.[A-Za-z]+|<[^>]+>|\b(?:Globulation 2|Glob2|JavaScript|JSON|UTF-8|SHA-256|SDL|NUL|CC0-1\.0|CC-BY(?:-SA)?-4\.0)\b')


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def hash_json(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True).encode()).hexdigest()


def protect(text, literals=()):
    """Protect tokens in HTML spans; the return parser restores by stable span ID."""
    leading = text[:len(text) - len(text.lstrip())]
    trailing = text[len(text.rstrip()):] if text.rstrip() != text else ''
    body = text.strip()
    pattern = re.compile('|'.join([re.escape(v) for v in sorted(set(literals), key=len, reverse=True) if v]
                                  + [TOKENS.pattern, TECHNICAL.pattern]))
    pieces, protected, position = [], [], 0
    for match in pattern.finditer(body):
        pieces.append(html.escape(body[position:match.start()]))
        index = len(protected)
        protected.append(match.group())
        pieces.append(f'<span id="g2p{index}" class="notranslate" translate="no">{html.escape(match.group())}</span>')
        position = match.end()
    pieces.append(html.escape(body[position:]))
    return ''.join(pieces), protected, leading, trailing


class Restorer(HTMLParser):
    def __init__(self, protected):
        super().__init__(convert_charrefs=True)
        self.protected = protected
        self.parts = []
        self.seen = Counter()
        self.in_span = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        marker = attrs.get('id', '')
        if tag != 'span' or self.in_span or not re.fullmatch(r'g2p[0-9]+', marker):
            raise ValueError('Translation changed protected HTML structure')
        index = int(marker[3:])
        if index >= len(self.protected):
            raise ValueError('Unknown protected span')
        self.parts.append(self.protected[index])
        self.seen[index] += 1
        self.in_span = True

    def handle_endtag(self, tag):
        if tag != 'span' or not self.in_span:
            raise ValueError('Translation changed protected HTML structure')
        self.in_span = False

    def handle_data(self, data):
        if not self.in_span:
            self.parts.append(data)

    def finish(self):
        self.close()
        if self.in_span or self.seen != Counter(range(len(self.protected))):
            raise ValueError('Translation lost or duplicated protected spans')
        return ''.join(self.parts)


def restore(translated, protection, source, *, native=True):
    _, protected, leading, trailing = protection
    parser = Restorer(protected)
    parser.feed(translated)
    text = leading + parser.finish().strip() + trailing
    if not text.strip() or any(c in text for c in ('\ufffd', '\x00', '\r', '\n')):
        raise ValueError('Empty or invalid provider translation')
    if Counter(TOKENS.findall(text)) != Counter(TOKENS.findall(source)):
        raise ValueError('Provider translation changed placeholders')
    if native and PROGRAM.CHECKER.invalid_percent(text) and not PROGRAM.CHECKER.invalid_percent(source):
        raise ValueError('Provider translation introduced invalid percent tokens')
    return text


class Campaign:
    def __init__(self, folder, project, locale, source_hash, budget, rate):
        self.folder = folder
        self.project, self.locale = project, locale
        self.budget, self.rate = Decimal(str(budget)), Decimal(str(rate))
        if not self.budget.is_finite() or not self.rate.is_finite() or self.budget <= 0 or self.rate <= 0:
            raise ValueError('Budget and price must be positive')
        self.identity = {'project': project, 'locale': locale, 'source_sha256': source_hash}
        self.path = folder / 'ledger.json'
        self.ledger = json.loads(self.path.read_text(encoding='utf-8')) if self.path.exists() else {**self.identity, 'attempts': []}
        if any(self.ledger.get(key) != value for key, value in self.identity.items()):
            raise ValueError('Campaign source, project or locale changed; use a separate evidence directory')
        self.token = None

    @property
    def spent(self):
        return sum((Decimal(row['reserved_usd']) for row in self.ledger['attempts']), Decimal(0))

    def reserve(self, request_hash, characters):
        charge = Decimal(characters) * self.rate / 1_000_000
        if self.spent + charge > self.budget:
            raise ValueError(f'Campaign budget exhausted: reserved US${self.spent}; next request US${charge}')
        row = {'request_sha256': request_hash, 'characters': characters, 'usd_per_million': str(self.rate),
               'reserved_usd': str(charge), 'state': 'reserved', 'time_unix': time.time()}
        self.ledger['attempts'].append(row)
        atomic_json(self.path, self.ledger)
        return row

    def send(self, request):
        if not self.token:
            self.token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True).strip()
        url = f'https://translation.googleapis.com/v3/projects/{self.project}/locations/global:translateText'
        headers = {'Authorization': 'Bearer ' + self.token, 'x-goog-user-project': self.project,
                   'Content-Type': 'application/json; charset=utf-8'}
        with urllib.request.urlopen(urllib.request.Request(url, json.dumps(request).encode(), headers), timeout=90) as response:
            return json.load(response)

    def translate(self, contents):
        request = {'sourceLanguageCode': 'en', 'targetLanguageCode': self.locale,
                   'mimeType': 'text/html', 'contents': contents}
        request_hash = hash_json({**self.identity, 'request': request})
        cache = self.folder / 'requests' / (request_hash + '.json')
        if cache.exists():
            result = json.loads(cache.read_text(encoding='utf-8'))
            if result.get('identity') != self.identity or result.get('request') != request:
                raise ValueError('Cached request identity mismatch')
            return result['response']
        characters = sum(len(content) for content in contents)
        for attempt in range(3):
            row = self.reserve(request_hash, characters)
            try:
                response = self.send(request)
            except urllib.error.HTTPError as exc:
                exc.close()
                row['state'], row['http_status'] = 'http_error', exc.code
                atomic_json(self.path, self.ledger)
                if exc.code not in (429, 500, 502, 503, 504) or attempt == 2:
                    raise RuntimeError(f'Google Translation HTTP {exc.code}; see campaign ledger') from None
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                row['state'] = 'ambiguous_network_error'
                atomic_json(self.path, self.ledger)
                if attempt == 2:
                    raise RuntimeError('Google Translation network failure; attempted costs remain reserved') from None
            else:
                row['state'] = 'response_received'
                # Cache actual provider output before interpreting its structure.
                atomic_json(cache, {'identity': self.identity, 'request': request, 'response': response})
                atomic_json(self.path, self.ledger)
                return response
            time.sleep(2 ** attempt)
        raise AssertionError('Unreachable retry exit')


def pending_entries(native_source, online_source, native, online, invariants):
    entries = []
    for key, source in native_source.items():
        if source.strip() and not native[key].strip():
            entries.append(('native', key, None, source, ()))
    for key, value in online_source.items():
        # Thai has only the CLDR "other" plural category.
        variants = {None: value} if isinstance(value, str) else {'other': value['other']}
        for category, source in variants.items():
            current = online[key] if category is None else online[key].get(category, '')
            if not current.strip():
                entries.append(('online', key, category, source, invariants.get(key, ())))
    return entries


@contextmanager
def campaign_lock(folder):
    """Refuse simultaneous draft/ledger writers, including across processes.

    An interrupted process may leave this lock. Its PID is evidence for an
    operator to check before explicitly removing the stale lock and resuming.
    """
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / 'campaign.lock'
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        raise ValueError(f'Campaign is locked: {path}; check the recorded PID before removing a stale lock') from None
    try:
        with os.fdopen(descriptor, 'w') as handle:
            handle.write(str(os.getpid()) + '\n')
        yield
    finally:
        path.unlink()


def run(args):
    with campaign_lock(args.output / 'google' / args.locale):
        return run_locked(args)


def run_locked(args):
    output = args.output
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if args.locale not in manifest['locales'] or args.locale != 'th':
        raise ValueError('This adapter currently supports a prepared Thai locale only')
    _, current_native, current_online, _, current_hash = PROGRAM.sources()
    if current_hash != manifest['source_sha256']:
        raise ValueError('Frozen English sources no longer match checkout; prepare a new kit')
    native_source = json.loads((output / 'source/native.json').read_text(encoding='utf-8'))
    online_source = json.loads((output / 'source/online.json').read_text(encoding='utf-8'))
    if native_source != current_native or online_source != current_online:
        raise ValueError('Frozen source snapshots were changed; prepare a new kit')
    paths = PROGRAM.catalog_paths(output, args.locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    online = json.loads(paths['online'].read_text(encoding='utf-8'))
    if errors or set(native) != set(native_source) or set(online) != set(online_source):
        raise ValueError('Draft catalog keys are invalid')
    invariants = json.loads((ROOT / 'services/online/packages/i18n/invariants.json').read_text(encoding='utf-8'))
    entries = pending_entries(native_source, online_source, native, online, invariants)
    if args.ids_file:
        selected = json.loads(args.ids_file.read_text(encoding='utf-8'))
        if not isinstance(selected, list) or not all(isinstance(key, str) for key in selected) or len(set(selected)) != len(selected):
            raise ValueError('--ids-file must contain a JSON array of distinct surface:key IDs')
        known = {'native:' + key for key in native_source} | {'online:' + key for key in online_source}
        if set(selected) - known:
            raise ValueError('--ids-file contains unknown source keys')
        entries = [entry for entry in entries if entry[0] + ':' + entry[1] in set(selected)]
    if args.limit is not None:
        if args.limit < 1:
            raise ValueError('--limit must be positive')
        entries = entries[:args.limit]
    campaign = Campaign(output / 'google' / args.locale, args.project, args.locale,
                        manifest['source_sha256'], args.budget_usd, args.usd_per_million)
    count = 0
    batch, protections, size = [], [], 0

    def flush():
        nonlocal batch, protections, size, count
        if not batch:
            return
        response = campaign.translate([protection[0] for protection in protections])
        translated = response.get('translations', [])
        if len(translated) != len(batch):
            raise ValueError('Provider response entry count differs from request')
        # Validate entire response before changing any in-memory draft value.
        values = [restore(item.get('translatedText', ''), protection, entry[3], native=entry[0] == 'native')
                  for entry, protection, item in zip(batch, protections, translated)]
        for (surface, key, category, _, literals), value in zip(batch, values):
            if any(literal not in value for literal in literals):
                raise ValueError('Provider translation lost a required literal')
            if surface == 'native':
                native[key] = value
            elif category is None:
                online[key] = value
            else:
                online[key][category] = value
        temporary = paths['native'].with_suffix('.tmp')
        temporary.write_text(''.join(key + '\n' + native[key] + '\n' for key in native), encoding='utf-8')
        temporary.replace(paths['native'])
        atomic_json(paths['online'], online)
        count += len(batch)
        print(json.dumps({'completed_this_run': count, 'reserved_usd': str(campaign.spent)}), flush=True)
        batch, protections, size = [], [], 0

    for entry in entries:
        protection = protect(entry[3], entry[4])
        length = len(protection[0])
        if length > 4000:
            raise ValueError(f'Protected entry exceeds 4000 characters: {entry[0]}:{entry[1]}')
        if batch and (size + length > 4000 or len(batch) >= 80):
            flush()
        batch.append(entry)
        protections.append(protection)
        size += length
    flush()
    check = PROGRAM.check_draft(output, args.locale)
    report_path = campaign.folder / 'check-draft.json'
    atomic_json(report_path, check)
    return {'translated_this_run': count, 'reserved_usd': str(campaign.spent),
            'errors': len(check['errors']), 'untranslated': len(check['untranslated']),
            'same_as_english_review_queue': len(check['same_as_english_review_queue']),
            'report': str(report_path)}


def campaign_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', required=True)
    parser.add_argument('--locale', choices=('th',), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--budget-usd', type=Decimal, required=True)
    parser.add_argument('--usd-per-million', type=Decimal, default=Decimal('20'))
    parser.add_argument('--ids-file', type=Path, help='JSON array of surface:key IDs for a representative pilot; filters before --limit')
    parser.add_argument('--limit', type=int, help='Translate only this many currently blank entries (pilot)')
    args = parser.parse_args()
    try:
        result = run(args)
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps(result, ensure_ascii=False))



# Estimate-first plans retain the upstream request encoding and character ledger.
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


def plan_protect(text):
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


def plan_restore(source, translated):
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
        encoded = plan_protect(text)
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
    return {source: plan_restore(source, value['translatedText']) for source, value in zip(chunk, values)}


def _translate_planned(plan, output, client, target, max_characters, retries=2, retry_invalid=False):
    folder = output / 'google' / plan['locale']
    cache, attempts = folder / 'responses', folder / 'attempts'
    cache.mkdir(parents=True, exist_ok=True)
    attempts.mkdir(parents=True, exist_ok=True)
    translated, requests_made = {}, 0
    for chunk in plan['chunks']:
        payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': target,
                   'mimeType': 'text/html', 'contents': [plan_protect(text) for text in chunk]}
        key = cache_key(payload, client.project, client.location)
        path = cache / (key + '.json')
        if path.exists():
            record = json.loads(path.read_text(encoding='utf-8'))
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
                charged = sum(json.loads(p.read_text(encoding='utf-8'))['characters'] for p in attempts.glob('*.json'))
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


def translate(plan, output, client, target, max_characters, retries=2, retry_invalid=False):
    """Serialize plan writers without altering paid request/cache identities."""
    with campaign_lock(output / 'google' / plan['locale']):
        return _translate_planned(plan, output, client, target, max_characters, retries, retry_invalid)


def plan_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing translation-program kit')
    parser.add_argument('--locale', default='nb')
    parser.add_argument('--project')
    parser.add_argument('--location', default='global')
    parser.add_argument('--batches', nargs='+', help='Selected pilot batch IDs; omit for all')
    parser.add_argument('--request-characters', type=int, default=4000)
    parser.add_argument('--max-characters', type=int, help='Cumulative attempted characters, including retries and prior runs')
    parser.add_argument('--price-per-million', type=float, default=20, help='Planning estimate only; verify current project pricing')
    parser.add_argument('--retry-invalid-responses', action='store_true', help='Archive invalid cached outputs and retry them within the same budget')
    parser.add_argument('--execute', action='store_true', help='Make billable requests; default only estimates')
    args = parser.parse_args()
    try:
        if not 1 <= args.request_characters <= 30000 or args.price_per_million < 0:
            raise ValueError('Invalid request size or price')
        plan = make_plan(args.output, args.locale, args.batches, args.request_characters)
        characters = sum(len(plan_protect(text)) for chunk in plan['chunks'] for text in chunk)
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



def main():
    # Preserve both existing CLI contracts and their distinct immutable caches.
    # The explicit dollar ceiling selects the original Thai campaign workflow.
    dollar_campaign = any(value == '--budget-usd' or value.startswith('--budget-usd=')
                          for value in sys.argv[1:])
    return campaign_main() if dollar_campaign else plan_main()


if __name__ == '__main__':
    raise SystemExit(main())
