#!/usr/bin/env python3
"""Create resumable Google Cloud machine drafts without changing reviewed catalogs."""
import argparse
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


def main():
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
