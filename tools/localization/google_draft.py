#!/usr/bin/env python3
"""Resumable, budget-bounded Google v3 drafts for a frozen translation-program kit.

Only development artifacts are written. Review records and registered catalogs are
never changed. Every attempted request is reserved before transmission, including
ambiguous network failures. The local ceiling applies to this campaign, not to
other consumers of the Google Cloud billing account.
"""
import argparse
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
TECHNICAL = re.compile(r'\\n|:|\[[^\]\r\n]+\]|\b(?:Ctrl|Alt|Shift|Meta|Cmd)\+(?:[A-Za-z0-9+]+)|https?://[^\s<>]+|[\w.+-]+@[\w.-]+\.[A-Za-z]+|<[^>]+>|\b(?:Globulation 2|Glob2|JavaScript|JSON|UTF-8|SHA-256|SDL|NUL|CC0-1\.0|CC-BY(?:-SA)?-4\.0)\b')


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
        self.ledger = json.loads(self.path.read_text()) if self.path.exists() else {**self.identity, 'attempts': []}
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
            result = json.loads(cache.read_text())
            if result.get('identity') != self.identity or result.get('request') != request:
                raise ValueError('Cached request identity mismatch')
            return result['response']
        characters = sum(len(content) for content in contents)
        for attempt in range(3):
            row = self.reserve(request_hash, characters)
            try:
                response = self.send(request)
            except urllib.error.HTTPError as exc:
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
    manifest = json.loads((output / 'manifest.json').read_text())
    if args.locale not in manifest['locales'] or args.locale != 'th':
        raise ValueError('This adapter currently supports a prepared Thai locale only')
    _, current_native, current_online, _, current_hash = PROGRAM.sources()
    if current_hash != manifest['source_sha256']:
        raise ValueError('Frozen English sources no longer match checkout; prepare a new kit')
    native_source = json.loads((output / 'source/native.json').read_text())
    online_source = json.loads((output / 'source/online.json').read_text())
    if native_source != current_native or online_source != current_online:
        raise ValueError('Frozen source snapshots were changed; prepare a new kit')
    paths = PROGRAM.catalog_paths(output, args.locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    online = json.loads(paths['online'].read_text())
    if errors or set(native) != set(native_source) or set(online) != set(online_source):
        raise ValueError('Draft catalog keys are invalid')
    invariants = json.loads((ROOT / 'platform/packages/i18n/invariants.json').read_text())
    entries = pending_entries(native_source, online_source, native, online, invariants)
    if args.ids_file:
        selected = json.loads(args.ids_file.read_text())
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


def main():
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


if __name__ == '__main__':
    main()
