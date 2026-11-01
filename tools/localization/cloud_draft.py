#!/usr/bin/env python3
"""Fill blank translation-kit drafts using gcloud-authenticated Translation v3.

Provider evidence and resumable caches stay inside the ignored translation kit.
This command creates machine drafts, never linguistic approvals.
"""
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

SPEC = importlib.util.spec_from_file_location('translation_program', Path(__file__).with_name('translation_program.py'))
PROGRAM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROGRAM)
LIMIT = 4500
TOKEN = re.compile(r'%\d+|\{[a-zA-Z][a-zA-Z0-9_]*\}|\\n|\n|\r|<[^>]+>')
SENTINEL = re.compile(r'GLOB2KEEP\d+END')


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def protect(text, invariants=()):
    if SENTINEL.search(text):
        raise ValueError('Source contains reserved protection sentinel')
    literals = sorted(set(invariants), key=len, reverse=True)
    pattern = re.compile('|'.join(re.escape(value) for value in literals) + ('|' if literals else '') + TOKEN.pattern)
    protected, position, replacements = [], 0, {}
    for match in pattern.finditer(text):
        protected.append(escape(text[position:match.start()]))
        token = f'GLOB2KEEP{len(replacements)}END'
        replacements[token] = match.group()
        protected.append(f'<span translate="no" class="notranslate">{token}</span>')
        position = match.end()
    protected.append(escape(text[position:]))
    return ''.join(protected), replacements


class PlainText(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_data(self, data):
        self.parts.append(data)


def restore(text, replacements):
    parser = PlainText()
    parser.feed(text)
    parser.close()
    result = ''.join(parser.parts)
    if Counter(SENTINEL.findall(result)) != Counter(replacements.keys()):
        raise ValueError('Provider changed, removed or duplicated protected literals')
    return SENTINEL.sub(lambda match: replacements[match.group()], result)


def split_text(text, invariants=(), limit=LIMIT):
    """Split only between words, never within placeholders or invariant literals."""
    if len(protect(text, invariants)[0]) <= limit:
        return [text]
    # Identify spans which may not be split, including multi-word invariants.
    spans = [match.span() for match in TOKEN.finditer(text)]
    for literal in invariants:
        spans.extend(match.span() for match in re.finditer(re.escape(literal), text))
    boundaries = [match.end() for match in re.finditer(r'\s+', text)
                  if not any(start < match.end() < end for start, end in spans)]
    output, start = [], 0
    while start < len(text):
        candidates = [end for end in boundaries + [len(text)] if end > start]
        end = None
        for candidate in candidates:
            if len(protect(text[start:candidate], invariants)[0]) > limit:
                break
            end = candidate
        if end is None:
            raise ValueError('An indivisible source token exceeds the request size limit')
        output.append(text[start:end])
        start = end
    return output


def payload(project, locale, contents):
    return {'sourceLanguageCode': 'en', 'targetLanguageCode': locale, 'mimeType': 'text/html',
            'model': f'projects/{project}/locations/global/models/general/nmt', 'contents': contents}


def translate(project, body, attempts=4):
    token = subprocess.check_output(['gcloud', 'auth', 'print-access-token', '--project', project], text=True).strip()
    request = Request(f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText',
                      data=json.dumps(body).encode(), method='POST', headers={
                          'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json; charset=utf-8',
                          'x-goog-user-project': project})
    for attempt in range(attempts):
        try:
            with urlopen(request, timeout=90) as response:
                return json.load(response)
        except HTTPError as exc:
            if exc.code not in (429, 500, 502, 503, 504) or attempt == attempts - 1:
                raise RuntimeError(f'Translation API HTTP {exc.code}; no draft was overwritten') from None
        except (URLError, TimeoutError):
            if attempt == attempts - 1:
                raise RuntimeError('Translation API connection failed after bounded retries') from None
        time.sleep(min(2 ** attempt, 8))
    raise AssertionError('unreachable')


def frozen_invariants(output, manifest):
    path = output / 'source/invariants.json'
    expected = manifest['source_files']['platform/packages/i18n/invariants.json']
    if not path.exists():
        data = (PROGRAM.ROOT / 'platform/packages/i18n/invariants.json').read_bytes()
        if PROGRAM.digest(data) != expected:
            raise ValueError('Invariant source changed since the kit was frozen')
        path.write_bytes(data)
    if PROGRAM.digest(path.read_bytes()) != expected:
        raise ValueError('Frozen invariants hash mismatch')
    return json.loads(path.read_text(encoding='utf-8'))


def save_catalogs(paths, native, online, keys):
    path = paths['native']
    temporary = path.with_suffix('.tmp')
    temporary.write_text(''.join(key + '\n' + native[key] + '\n' for key in keys), encoding='utf-8')
    temporary.replace(path)
    atomic_json(paths['online'], online)


def response_translations(response, count):
    translations = response.get('translations') if isinstance(response, dict) else None
    if not isinstance(translations, list) or len(translations) != count or any(
            not isinstance(value, dict) or not isinstance(value.get('translatedText'), str)
            or not value['translatedText'].strip() for value in translations):
        raise ValueError('Provider response has incorrect cardinality or empty/non-string text')
    return translations


def cached_contents(cache, source_hash, project, locale):
    """Index original provider evidence by content so changed batch boundaries resume."""
    scope = payload(project, locale, [])
    scope.pop('contents')
    found = {}
    for path in sorted(cache.glob('*.json')):
        if not re.fullmatch(r'[0-9a-f]{64}', path.stem):
            continue
        saved = json.loads(path.read_text(encoding='utf-8'))
        identity = saved.get('identity', {})
        if hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest() != path.stem:
            raise ValueError('Cached request identity hash mismatch')
        request = identity.get('request', {})
        if identity.get('source_sha256') != source_hash or {key: value for key, value in request.items() if key != 'contents'} != scope:
            continue
        contents = request.get('contents', [])
        translations = response_translations(saved.get('response'), len(contents))
        for content, value in zip(contents, translations):
            found.setdefault(content, value)
    return found


def run(output, locale, project, dry_run=False, selected_keys=None, limit=None):
    if not re.fullmatch(r'[a-z][a-z0-9-]{4,61}[a-z0-9]', project):
        raise ValueError('An explicit Google Cloud project ID is required')
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if locale not in manifest['locales']:
        raise ValueError('Locale is not part of this translation kit')
    paths = PROGRAM.catalog_paths(output, locale)
    native, errors = PROGRAM.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('; '.join(errors))
    online = json.loads(paths['online'].read_text(encoding='utf-8'))
    sources = {surface: json.loads((output / f'source/{surface}.json').read_text(encoding='utf-8'))
               for surface in ('native', 'online')}
    for surface, source_path in (('native', 'data/texts.en.txt'), ('online', 'platform/packages/i18n/locales/en.json')):
        canonical = PROGRAM.ROOT / source_path
        if PROGRAM.digest(canonical.read_bytes()) != manifest['source_files'][source_path]:
            raise ValueError('Canonical English source changed; freeze a new kit')
        canonical_values = PROGRAM.CHECKER.read_table(canonical)[0] if surface == 'native' else json.loads(canonical.read_text())
        if canonical_values != sources[surface]:
            raise ValueError('Frozen source snapshot was modified')
    invariants = frozen_invariants(output, manifest)
    pending = []
    for surface, target in (('native', native), ('online', online)):
        if set(target) != set(sources[surface]):
            raise ValueError(f'{surface}: draft keys differ from frozen source')
        for key, original in sources[surface].items():
            if selected_keys is not None and surface + ':' + key not in selected_keys:
                continue
            values = {'': target[key]} if isinstance(original, str) else target[key]
            if not isinstance(values, dict):
                raise ValueError('Plural draft must be an object')
            for category, current in values.items():
                if not isinstance(current, str):
                    raise ValueError('Draft value must be a string')
                if current.strip():
                    continue
                text = original if isinstance(original, str) else original.get(category, original['other'])
                if not text.strip():
                    continue
                chunks = split_text(text, invariants.get(key, ()) if surface == 'online' else ())
                pending.append({'surface': surface, 'key': key, 'category': category, 'text': text,
                                'chunks': chunks, 'invariants': invariants.get(key, ()) if surface == 'online' else ()})
    if selected_keys is not None:
        known = {surface + ':' + key for surface, values in sources.items() for key in values}
        if set(selected_keys) - known:
            raise ValueError('Pilot key selection contains unknown source IDs')
    if limit is not None:
        if limit < 1:
            raise ValueError('Pilot limit must be positive')
        pending = pending[:limit]
    report = {'locale': locale, 'project': project, 'pending_values': len(pending),
              'source_characters': sum(len(item['text']) for item in pending), 'requests': 0, 'cache_hits': 0}
    if dry_run:
        return report
    cache = output / 'cloud' / locale
    cached = cached_contents(cache, manifest['source_sha256'], project, locale)
    jobs = []
    for item in pending:
        item['results'] = [None] * len(item['chunks'])
        for index, chunk in enumerate(item['chunks']):
            leading = chunk[:len(chunk) - len(chunk.lstrip())]
            trailing = chunk[len(chunk.rstrip()):]
            protected, replacements = protect(chunk.strip(), item['invariants'])
            if not protected:
                item['results'][index] = chunk
            else:
                jobs.append((item, index, protected, replacements, leading, trailing))
    batches, batch, size = [], [], 0
    for job in jobs:
        if batch and (size + len(job[2]) > LIMIT or len(batch) >= 1000):
            batches.append(batch)
            batch, size = [], 0
        batch.append(job)
        size += len(job[2])
    if batch:
        batches.append(batch)
    completed = 0
    for batch in batches:
        missing = list(dict.fromkeys(job[2] for job in batch if job[2] not in cached))
        if missing:
            body = payload(project, locale, missing)
            identity = {'source_sha256': manifest['source_sha256'], 'request': body}
            request_hash = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
            evidence = cache / (request_hash + '.json')
            response = translate(project, body)
            atomic_json(evidence, {'identity': identity, 'response': response})
            report['requests'] += 1
            translations = response_translations(response, len(missing))
            cached.update(zip(missing, translations))
        else:
            report['cache_hits'] += 1
        translations = [cached[job[2]] for job in batch]
        # Validate the entire response before changing either draft catalog.
        restored = [job[4] + restore(value['translatedText'], job[3]).strip() + job[5]
                    for job, value in zip(batch, translations)]
        if any(not value.strip() for value in restored):
            raise ValueError('Provider returned an empty translated chunk')
        for job, value in zip(batch, restored):
            item, index = job[:2]
            item['results'][index] = value
            if any(part is None for part in item['results']):
                continue
            value = ''.join(item['results'])
            if not value.strip() or Counter(TOKEN.findall(value)) != Counter(TOKEN.findall(item['text'])):
                raise ValueError('Translation is blank or changed formatting tokens')
            if ('\n' in value or '\r' in value) and item['surface'] == 'native':
                raise ValueError('Native translation contains physical line breaks')
            target = native if item['surface'] == 'native' else online
            if item['category']:
                target[item['key']][item['category']] = value
            else:
                target[item['key']] = value
            completed += 1
        save_catalogs(paths, native, online, list(sources['native']))
        print(json.dumps({'completed_values': completed, **report}), flush=True)
    atomic_json(cache / 'summary.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--locale', required=True, choices=PROGRAM.LOCALES)
    parser.add_argument('--project', required=True)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--limit', type=int, help='draft at most this many pending values for a pilot')
    parser.add_argument('--keys-file', type=Path, help='JSON array of native:/online: source IDs for a representative pilot')
    args = parser.parse_args()
    try:
        selected = json.loads(args.keys_file.read_text()) if args.keys_file else None
        if selected is not None and (not isinstance(selected, list) or any(not isinstance(key, str) for key in selected)):
            raise ValueError('Keys file must be a JSON array of source IDs')
        print(json.dumps(run(args.output, args.locale, args.project, args.dry_run, selected, args.limit), ensure_ascii=False))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'{type(exc).__name__}: {exc}\n')


if __name__ == '__main__':
    main()
