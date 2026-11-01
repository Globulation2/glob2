#!/usr/bin/env python3
"""Create unregistered machine drafts from a frozen translation-program kit.

Dry-run by default. Cloud credentials stay in memory; API responses, accounting
and caches stay inside the ignored kit. This does not create review approvals.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.error
import urllib.request

import translation_program as program

# Native catalogs use %0 etc.; online catalogs use named brace substitutions.
# Protect structural markup, technical paths and literal backslash escapes too.
PROTECTED = re.compile(
    r'https?://[^\s<>"\']+|<[^>\n]+>|\{[a-zA-Z][a-zA-Z0-9_]*\}'
    r'|%(?:[0-9]+|(?:[0-9]+\$)?[-+#0 ]*[0-9.*]*[hlLzjt]*[diouxXeEfFgGcrsa%])'
    r'|\\(?:[nrt\\]|u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2})'
    r'|(?:[\w-]+/)+[\w.%-]+|\b[\w-]+\.(?:js|json|wasm|txt|zip|map|png|jpg|ogg|mp3|webp|ttf)\b'
)
MARKER = re.compile(r'GLOB2KEEP[0-9]+END')
RATE_PER_MILLION = Decimal('20')
MAX_REQUEST_ITEMS = 100
RECOMMENDED_REQUEST_CODEPOINTS = 5000


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def protect(text):
    if 'GLOB2KEEP' in text:
        raise ValueError('Source collides with protected-token marker')
    tokens = []

    def replace(match):
        tokens.append(match.group())
        return f'GLOB2KEEP{len(tokens) - 1}END'

    return PROTECTED.sub(replace, text), tokens


def restore(text, tokens):
    expected = Counter(f'GLOB2KEEP{i}END' for i in range(len(tokens)))
    if Counter(MARKER.findall(text)) != expected:
        raise ValueError('Cloud response damaged protected tokens; retained for diagnosis only')
    result = MARKER.sub(lambda match: tokens[int(match.group()[9:-3])], text)
    if 'GLOB2KEEP' in result or '\ufffd' in result or '\x00' in result:
        raise ValueError('Invalid cloud translation')
    if not result.strip():
        raise ValueError('Empty cloud translation')
    return result


def needs_translation(source):
    """Formatting-only labels have no prose for an engine to translate."""
    return any(character.isalpha() for character in PROTECTED.sub('', source))


def accept_response(response, group, cache, rejected):
    translations = response.get('translations', [])
    if len(translations) != len(group):
        raise ValueError('Cloud response count differs from request')
    for (source, (_, tokens)), item in zip(group, translations):
        try:
            restore(item['translatedText'], tokens)
        except (ValueError, KeyError, TypeError):
            rejected[source] = item
        else:
            cache[source] = {'translatedText': item['translatedText']}
            rejected.pop(source, None)


@contextmanager
def exclusive(path):
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        raise ValueError('Draft runner lock exists; check for another runner before removing it') from None
    try:
        os.write(descriptor, str(os.getpid()).encode())
        os.close(descriptor)
        yield
    finally:
        path.unlink()


class Cloud:
    def __init__(self, project, locale):
        self.url = f'https://translation.googleapis.com/v3/projects/{project}/locations/global:translateText'
        self.project = project
        self.locale = locale

    def translate(self, contents):
        # Do not print commands, headers, credentials or authentication errors.
        token = subprocess.check_output(['gcloud', 'auth', 'print-access-token'],
                                        stderr=subprocess.DEVNULL, text=True).strip()
        body = json.dumps({'sourceLanguageCode': 'en', 'targetLanguageCode': self.locale,
                           'mimeType': 'text/plain', 'contents': contents}).encode()
        request = urllib.request.Request(self.url, data=body, headers={
            'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json; charset=utf-8',
            'x-goog-user-project': self.project})
        with urllib.request.urlopen(request, timeout=90) as response:
            return json.load(response)


def entries(output, locale, phase, pilot_size):
    manifest = json.loads((output / 'manifest.json').read_text())
    if locale not in manifest['locales']:
        raise ValueError('Locale is not part of this kit')
    # Check frozen source copies, not mutable live source files. The manifest
    # remains the revision authority; later source changes need a new kit.
    native = json.loads((output / 'source/native.json').read_text())
    online = json.loads((output / 'source/online.json').read_text())
    paths = program.catalog_paths(output, locale)
    targets, errors = program.CHECKER.read_table(paths['native'])
    if errors:
        raise ValueError('Invalid native draft: ' + '; '.join(errors))
    target_online = json.loads(paths['online'].read_text())
    if set(targets) != set(native) or set(target_online) != set(online):
        raise ValueError('Draft/source inventory mismatch')
    selected = []
    seen_domains = set()
    categories = None
    for spec in manifest['batches']:
        batch = json.loads((output / spec['path']).read_text())
        if batch['source_sha256'] != manifest['source_sha256']:
            raise ValueError('Stale source batch')
        domain = (batch['surface'], batch['domain'])
        if phase == 'pilot' and domain in seen_domains:
            continue
        seen_domains.add(domain)
        batch_entries = batch['entries'][:pilot_size] if phase == 'pilot' else batch['entries']
        for entry in batch_entries:
            key, source = entry['key'], entry['source']
            frozen = native if batch['surface'] == 'native' else online
            if source != frozen[key]:
                raise ValueError('Frozen source copy differs from batch')
            target = targets[key] if batch['surface'] == 'native' else target_online[key]
            if isinstance(source, str):
                if not isinstance(target, str):
                    raise ValueError('Invalid draft value type')
                if not target.strip() and source.strip():
                    selected.append((batch['surface'], key, None, source))
            else:
                if categories is None:
                    categories = program.plural_categories(locale)
                if not isinstance(target, dict) or not set(categories).issubset(target):
                    raise ValueError('Invalid draft plural branches')
                for category in categories:
                    if not target[category].strip():
                        selected.append((batch['surface'], key, category,
                                         source.get(category, source['other'])))
    return manifest, paths, targets, target_online, selected


def write_drafts(paths, native, online, keys):
    # Fill only blank entries, preserving every existing translated/edit value.
    temporary = paths['native'].with_suffix('.tmp')
    temporary.write_text(''.join(key + '\n' + native[key] + '\n' for key in keys), encoding='utf-8')
    temporary.replace(paths['native'])
    atomic_json(paths['online'], online)


def run(output, locale, project, phase='pilot', pilot_size=3, cap=Decimal('10'),
        execute=False, pilot_reviewed=False, cloud=None, retries=2, sleep=time.sleep):
    if not re.fullmatch(r'[a-z][a-z0-9-]{4,61}[a-z0-9]', project):
        raise ValueError('Invalid Google Cloud project ID')
    if phase == 'bulk' and not pilot_reviewed:
        raise ValueError('Review the source-based pilot before passing --pilot-reviewed')
    if not cap.is_finite() or cap <= 0 or pilot_size < 1 or retries not in range(4):
        raise ValueError('Invalid spending cap, pilot size or retry limit')
    state = output / 'cloud' / locale
    state.mkdir(parents=True, exist_ok=True)
    with exclusive(state / 'runner.lock'):
        manifest, paths, native, online, selected = entries(output, locale, phase, pilot_size)
        ledger_path, cache_path = state / 'accounting.json', state / 'cache.json'
        rejected_path = state / 'rejected.json'
        ledger = json.loads(ledger_path.read_text()) if ledger_path.exists() else {
            'project': project, 'locale': locale, 'source_sha256': manifest['source_sha256'],
            'usd_per_million_characters': str(RATE_PER_MILLION), 'reserved_characters': 0, 'attempts': []}
        if (ledger['project'], ledger['locale'], ledger['source_sha256'],
            ledger['usd_per_million_characters']) != (project, locale, manifest['source_sha256'], str(RATE_PER_MILLION)):
            raise ValueError('Accounting configuration changed; retain original kit/accounting')
        cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
        rejected = json.loads(rejected_path.read_text()) if rejected_path.exists() else {}
        unique = {source: protect(source) for _, _, _, source in selected}
        # Recover an interrupted/partially invalid response without rebilling its
        # successful items. Original request sources are checkpointed from now
        # on; the hash-checked fallback supports the original small pilot.
        for index, record in enumerate(ledger['attempts'], start=1):
            if not execute:
                break
            response_path = state / 'responses' / f'{index:05d}.json'
            if record['status'] != 'reserved' or not response_path.exists():
                continue
            request_sources = record.get('sources', list(unique))
            group = [(source, protect(source)) for source in request_sources]
            request_hash = program.digest(json.dumps([data[0] for _, data in group]).encode())
            if request_hash != record['request_sha256']:
                raise ValueError('Cannot safely reconstruct interrupted request; retain response for manual recovery')
            accept_response(json.loads(response_path.read_text()), group, cache, rejected)
            record['status'] = 'completed-with-rejections' if any(source in rejected for source, _ in group) else 'completed'
            atomic_json(cache_path, cache)
            atomic_json(rejected_path, rejected)
            atomic_json(ledger_path, ledger)
        for source, (protected, _) in unique.items():
            if not needs_translation(source):
                cache[source] = {'translatedText': protected}
                rejected.pop(source, None)
        # Rejected responses need a human/agent correction, never another paid
        # attempt. Continue translating unrelated strings before reporting the
        # complete remaining rejection set at the end.
        missing = {source: data for source, data in unique.items() if source not in cache and source not in rejected}
        chars = sum(len(data[0]) for data in missing.values())
        cost = Decimal(ledger['reserved_characters'] + chars) * RATE_PER_MILLION / 1_000_000
        summary = {'phase': phase, 'blank_entries': len(selected), 'unique_uncached_texts': len(missing),
                   'new_characters': chars, 'reserved_characters': ledger['reserved_characters'],
                   'known_rejected_texts': sum(source in rejected for source in unique),
                   'estimated_total_usd_without_free_credit': str(cost), 'cap_usd': str(cap)}
        if cost > cap:
            raise ValueError('Estimated drafting cost exceeds spending ceiling')
        if not execute:
            return summary
        cloud = cloud or Cloud(project, locale)
        pending = list(missing.items())
        keys = (output / 'source/native-keys.txt').read_text().splitlines()

        def fill():
            nonlocal native, online
            # A reviewer may edit a draft while requests are in flight. Reload
            # the on-disk catalogs at each checkpoint and retain those edits.
            native, errors = program.CHECKER.read_table(paths['native'])
            online = json.loads(paths['online'].read_text())
            if errors or set(native) != set(keys):
                raise ValueError('Native draft changed structure during translation')
            for surface, key, category, source in selected:
                if source not in cache:
                    continue
                value = restore(cache[source]['translatedText'], protect(source)[1])
                if surface == 'native' and not native[key].strip():
                    if '\n' in value or '\r' in value:
                        raise ValueError('Cloud returned physical newline in native catalog')
                    native[key] = value
                elif surface == 'online' and category is None and not online[key].strip():
                    online[key] = value
                elif surface == 'online' and category is not None and not online[key][category].strip():
                    online[key][category] = value
            write_drafts(paths, native, online, keys)

        # Flush recovered and formatting-only values before sending requests,
        # so successful translations never need a duplicate paid call.
        atomic_json(cache_path, cache)
        atomic_json(rejected_path, rejected)
        fill()
        # At most 5k code points and 100 content items per request. Long single
        # source entries retain Google's absolute 30k limit.
        while pending:
            group, size = [], 0
            while pending and len(group) < MAX_REQUEST_ITEMS:
                item = pending[0]
                length = len(item[1][0])
                if length > 30000:
                    raise ValueError('Single translation exceeds API code-point limit')
                if group and size + length > RECOMMENDED_REQUEST_CODEPOINTS:
                    break
                group.append(pending.pop(0))
                size += length
            contents = [data[0] for _, data in group]
            for attempt in range(retries + 1):
                new_total = ledger['reserved_characters'] + size
                if Decimal(new_total) * RATE_PER_MILLION / 1_000_000 > cap:
                    raise ValueError('Retry/request would exceed spending ceiling')
                # Reserve before transmission: timeouts/crashes may have billed.
                ledger['reserved_characters'] = new_total
                record = {'characters': size, 'request_sha256': program.digest(json.dumps(contents).encode()),
                          'sources': [source for source, _ in group], 'status': 'reserved'}
                ledger['attempts'].append(record)
                atomic_json(ledger_path, ledger)
                try:
                    response = cloud.translate(contents)
                except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as exc:
                    code = exc.code if isinstance(exc, urllib.error.HTTPError) else None
                    record['status'] = 'http-' + str(code) if code else 'transport-error'
                    atomic_json(ledger_path, ledger)
                    if attempt == retries or (code is not None and code != 429 and code < 500):
                        raise ValueError('Translation request failed; see accounting status') from None
                    sleep(min(2 ** attempt, 8))
                    continue
                atomic_json(state / 'responses' / f'{len(ledger["attempts"]):05d}.json', response)
                accept_response(response, group, cache, rejected)
                atomic_json(cache_path, cache)
                atomic_json(rejected_path, rejected)
                record['status'] = 'completed-with-rejections' if any(source in rejected for source, _ in group) else 'completed'
                atomic_json(ledger_path, ledger)
                fill()
                break
        fill()
        summary['reserved_characters'] = ledger['reserved_characters']
        summary['estimated_total_usd_without_free_credit'] = str(
            Decimal(ledger['reserved_characters']) * RATE_PER_MILLION / 1_000_000)
        active_rejections = [source for source in unique if source in rejected]
        if active_rejections:
            raise ValueError(f'{len(active_rejections)} protected-token rejections require manual draft correction; '
                             f'complete rejected text/response list: {rejected_path}')
        return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=program.ROOT / 'artifacts/bengali-localization')
    parser.add_argument('--locale', default='bn', choices=program.LOCALES)
    parser.add_argument('--project', required=True)
    parser.add_argument('--phase', choices=('pilot', 'bulk'), default='pilot')
    parser.add_argument('--pilot-size', type=int, default=3, help='entries per surface/domain in first batch')
    parser.add_argument('--pilot-reviewed', action='store_true', help='caller has independently reviewed pilot')
    parser.add_argument('--cap-usd', type=Decimal, default=Decimal('10'))
    parser.add_argument('--execute', action='store_true', help='make paid API requests (default: estimate only)')
    args = parser.parse_args()
    try:
        print(json.dumps(run(args.output, args.locale, args.project, args.phase, args.pilot_size,
                             args.cap_usd, args.execute, args.pilot_reviewed), indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        # subprocess errors could contain local credential paths; sanitize them.
        parser.exit(1, ('gcloud authentication failed' if isinstance(exc, subprocess.CalledProcessError)
                        else str(exc)) + '\n')


if __name__ == '__main__':
    main()
