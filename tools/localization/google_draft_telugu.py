#!/usr/bin/env python3
"""Resumable, gross-cost-capped Google v3 NMT drafts; development tooling only."""
import argparse
from collections import Counter
from contextlib import contextmanager
try:
    import fcntl
except ImportError:  # Read-only inventory and tests remain importable elsewhere.
    fcntl = None
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

import translation_program as program

RATE_MICRODOLLARS = 20  # $20 / million Unicode characters, ignoring free credits.
CAP_MICRODOLLARS = 9_000_000
TELUGU_METADATA = {
    '[language-code]': 'te',
    '[language]': 'తెలుగు',
    '[language incomplete]': 'తెలుగు — అసంపూర్ణం',
}
TOKEN = re.compile(r'%[0-9]+|\{[a-zA-Z][a-zA-Z0-9_]*\}')
NAMED_LITERALS = ('Globulation 2', 'Hive Mind', 'Castor', 'Cortex', 'Maxima',
                  'Nicowar', 'Numbi', 'Econo', 'Warrush', 'Cabino', 'JavaScript',
                  'OpenGL', 'WebGL', 'SDL')
TECHNICAL = re.compile(
    r'https?://[^\s<>"\']+|(?<!\w)/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+/?'
    r'|(?:[A-Za-z0-9_.-]+/){2,}[A-Za-z0-9_./-]+'
    r'|(?:data|scripts|browser|platform|src)/[A-Za-z0-9_./-]+'
    r'|[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+|\\[nrt]')


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w', encoding='utf-8') as handle:
        handle.write(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)
    directory = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


@contextmanager
def locked(folder):
    if fcntl is None:
        raise RuntimeError('Live translation runs require Linux fcntl locking')
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / 'run.lock').open('a') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def protect(text, invariants=()):
    literals = set(TOKEN.findall(text)) | set(TECHNICAL.findall(text)) | {item for item in invariants if item in text}
    names = {name for name in NAMED_LITERALS
             if re.search(r'(?<!\w)' + re.escape(name) + r'(?!\w)', text)}
    if 'GLOB2TOKEN' in text:
        raise ValueError('Source contains reserved token prefix')
    alternatives = [(item, False) for item in literals] + [(name, True) for name in names - literals]
    alternatives.sort(key=lambda item: (-len(item[0]), item[0]))
    pattern = re.compile('|'.join((r'(?<!\w)' + re.escape(item) + r'(?!\w)') if named else re.escape(item)
                                  for item, named in alternatives)) if alternatives else None
    replacements = []
    chunks = []
    offset = 0
    for match in pattern.finditer(text) if pattern else ():
        chunks.append(html.escape(text[offset:match.start()]))
        marker = f'GLOB2TOKEN{len(replacements):04d}'
        replacements.append((marker, match.group()))
        chunks.append(f'<span translate="no" class="notranslate">{marker}</span>')
        offset = match.end()
    chunks.append(html.escape(text[offset:]))
    return ''.join(chunks), replacements


class TextOnly(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.depth = 0

    def handle_starttag(self, tag, attrs):
        if tag != 'span' or self.depth:
            raise ValueError('Unexpected markup in translation')
        self.depth += 1

    def handle_endtag(self, tag):
        if tag != 'span' or self.depth != 1:
            raise ValueError('Malformed translation markup')
        self.depth -= 1

    def handle_data(self, data):
        self.parts.append(data)

    def handle_comment(self, data):
        raise ValueError('Unexpected comment in translation')


def restore(text, replacements):
    parser = TextOnly()
    parser.feed(text)
    parser.close()
    if parser.depth:
        raise ValueError('Unclosed translation markup')
    value = ''.join(parser.parts)
    if set(re.findall(r'GLOB2TOKEN\d+', value)) != {marker for marker, _ in replacements}:
        raise ValueError('Unknown or missing protected token')
    for marker, original in replacements:
        if value.count(marker) != 1:
            raise ValueError('Duplicated or damaged protected token')
        value = value.replace(marker, original)
    if 'GLOB2TOKEN' in value or '\ufffd' in value or '\x00' in value or not value.strip():
        raise ValueError('Invalid translated text')
    return value


def reserve(folder, project, payload):
    path = folder / 'ledger.json'
    ledger = json.loads(path.read_text()) if path.exists() else {'project': project, 'attempts': []}
    if ledger['project'] != project:
        raise ValueError('Cannot reuse ledger with another project')
    characters = sum(map(len, payload['contents']))
    cost = characters * RATE_MICRODOLLARS
    spent = sum(item['reserved_microdollars'] for item in ledger['attempts'])
    if spent + cost > CAP_MICRODOLLARS:
        raise ValueError('Gross $9 translation cap would be exceeded; no request sent')
    ledger['attempts'].append({'time': time.time(), 'request_sha256': program.digest(json.dumps(payload, sort_keys=True).encode()),
                               'characters': characters, 'reserved_microdollars': cost})
    atomic_json(path, ledger)  # No release on failure: uncertain outcomes may be billed.
    return spent + cost


def translate(folder, project, contents, send):
    payload = {'sourceLanguageCode': 'en', 'targetLanguageCode': 'te', 'mimeType': 'text/html',
               'model': f'projects/{project}/locations/global/models/general/nmt', 'contents': contents}
    key = program.digest(json.dumps(payload, sort_keys=True).encode())
    path = folder / 'responses' / (key + '.json')
    if path.exists():
        record = json.loads(path.read_text(encoding='utf-8'))
        if record['request'] != payload:
            raise ValueError('Cached request mismatch')
        response = record['response']
    else:
        reserve(folder, project, payload)
        response = send(payload)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Preserve even malformed successful API output, never overwrite raw evidence.
        with path.open('x', encoding='utf-8') as handle:
            json.dump({'request': payload, 'response': response}, handle, ensure_ascii=False, indent=2)
            handle.write('\n')
            handle.flush()
            os.fsync(handle.fileno())
    values = response.get('translations', [])
    if len(values) != len(contents) or any(not isinstance(v.get('translatedText'), str) for v in values):
        raise ValueError('Malformed Google response; raw response retained')
    return [v['translatedText'] for v in values]


def request_sender(project):
    token = None
    refreshed = 0.0
    def send(payload):
        nonlocal token, refreshed
        if token is None or time.monotonic() - refreshed >= 45 * 60:
            token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'], text=True).strip()
            refreshed = time.monotonic()
        request = urllib.request.Request(f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText',
                    data=json.dumps(payload).encode(), headers={'Authorization': 'Bearer ' + token,
                    'Content-Type': 'application/json', 'x-goog-user-project': project})
        with urllib.request.urlopen(request, timeout=90) as response:
            return json.load(response)
    return send


def _run(output, project, execute=False, expected_name='Bradley personal', pilot=False):
    manifest = json.loads((output / 'manifest.json').read_text())
    if 'te' not in manifest['locales'] or manifest['source_sha256'] != program.sources()[4]:
        raise ValueError('Telugu kit is absent or source snapshot is stale')
    online_source = json.loads((output / 'source/online.json').read_text())
    invariants = json.loads((program.ROOT / 'services/online/packages/i18n/invariants.json').read_text())
    native_source = json.loads((output / 'source/native.json').read_text())
    paths = program.catalog_paths(output, 'te')
    native, errors = program.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    # These identify the target catalog, rather than describing English. Sending
    # the English language code/name to NMT produces a mislabeled catalog.
    for key, value in TELUGU_METADATA.items():
        if key in native and not native[key].strip():
            native[key] = value
    online = json.loads(paths['online'].read_text())
    jobs = []
    for surface, source, target in [('native', native_source, native), ('online', online_source, online)]:
        for key, original in source.items():
            variants = target[key] if isinstance(target[key], dict) else {'other': target[key]}
            for category, value in variants.items():
                if value.strip() or (surface == 'native' and key in program.CHECKER.OPTIONAL_BLANKS and not original):
                    continue
                source_value = original if isinstance(original, str) else original.get(category, original['other'])
                protected, replacements = protect(source_value, invariants.get(key, []) if surface == 'online' else ())
                jobs.append((surface, key, category, protected, replacements))
    if pilot:
        selected = []
        groups = {}
        for job in jobs:
            group = (job[0], program.domain(job[0], job[1]))
            groups.setdefault(group, []).append(job)
        for entries in groups.values():
            selected.extend(entries[:3])
            selected.extend(sorted(entries, key=lambda job: len(job[3]), reverse=True)[:1])
        selected.extend([job for job in jobs if isinstance(online.get(job[1]), dict)][:6])
        jobs = list(dict.fromkeys(tuple(job[:4]) + (tuple(job[4]),) for job in selected))
    # Exact protected content deduplication: each key keeps its own restoration map.
    unique = list(dict.fromkeys(job[3] for job in jobs))
    estimate = sum(map(len, unique)) * RATE_MICRODOLLARS / 1_000_000
    print(json.dumps({'pending_variants': len(jobs), 'unique_requests': len(unique), 'gross_estimate_usd': estimate,
                      'cap_usd': CAP_MICRODOLLARS / 1_000_000}))
    if not execute:
        return
    project_info = json.loads(subprocess.check_output(['gcloud', 'projects', 'describe', project, '--format=json'], text=True))
    if project_info.get('name', '').casefold() != expected_name.casefold() or project_info.get('projectId') != project:
        raise ValueError('Project ID/display name does not match explicit Bradley project')
    folder = output / 'google'
    translated = {}
    for path in sorted((folder / 'responses').glob('*.json')):
        record = json.loads(path.read_text(encoding='utf-8'))
        request = record['request']
        if request.get('model') != f'projects/{project}/locations/global/models/general/nmt':
            raise ValueError('Cache project/model mismatch')
        values = record['response'].get('translations', [])
        if len(values) != len(request['contents']):
            raise ValueError('Malformed cached response')
        for source, value in zip(request['contents'], values):
            if not isinstance(value.get('translatedText'), str):
                raise ValueError('Malformed cached translation')
            translated[source] = value['translatedText']
    send = request_sender(project)
    batch = []
    def flush():
        if batch:
            translated.update(zip(batch, translate(folder, project, batch, send)))
            batch.clear()
    for value in unique:
        if value in translated:
            continue
        if batch and (len(batch) >= 200 or sum(map(len, batch)) + len(value) > 25000):
            flush()
        if len(value) > 25000:
            raise ValueError('Single protected input exceeds request limit')
        batch.append(value)
    flush()
    # Validate every result before changing either draft catalog.
    for surface, key, category, source, replacements in jobs:
        value = restore(translated[source], replacements)
        original = restore(source, replacements)
        if Counter(TOKEN.findall(value)) != Counter(TOKEN.findall(original)):
            raise ValueError('Translation introduced or damaged placeholders: ' + key)
        if surface == 'native' and ('\n' in value or '\r' in value):
            raise ValueError('Translation introduced native catalog line breaks: ' + key)
        for literal in invariants.get(key, []) if surface == 'online' else ():
            if value.count(literal) != original.count(literal):
                raise ValueError('Translation changed literal invariant count: ' + key)
        target = native if surface == 'native' else online
        if isinstance(target[key], dict):
            target[key][category] = value
        else:
            target[key] = value
    keys = (output / 'source/native-keys.txt').read_text().splitlines()
    temporary = paths['native'].with_suffix('.tmp')
    temporary.write_text(''.join(key + '\n' + native[key] + '\n' for key in keys), encoding='utf-8')
    temporary.replace(paths['native'])
    atomic_json(paths['online'], online)


def run(output, project, execute=False, expected_name='Bradley personal', pilot=False):
    if execute:
        with locked(output / 'google'):
            return _run(output, project, execute, expected_name, pilot)
    return _run(output, project, execute, expected_name, pilot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--project', required=True, help='Explicit project ID, never active gcloud default')
    parser.add_argument('--expected-project-name', default='Bradley personal')
    parser.add_argument('--run', action='store_true', help='Without this flag, print inventory/cost only')
    parser.add_argument('--pilot', action='store_true', help='Draft representative domain, long-text and plural samples first')
    args = parser.parse_args()
    run(args.output, args.project, args.run, args.expected_project_name, args.pilot)


if __name__ == '__main__':
    main()
