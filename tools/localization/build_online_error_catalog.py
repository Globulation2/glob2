#!/usr/bin/env python3
"""Embed only reviewed Bulgarian online strings for the native error presenter.

This is a build input generator, not another translation source. Do not edit its
output or expand its locale scope without reviewed catalogs and a size review.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

GENERIC = 'Something went wrong. Please try again.'
TOKEN = re.compile(r'\{([A-Za-z][A-Za-z0-9_]*)\}')


def generate(english_path, bulgarian_path, output):
    english_bytes, bulgarian_bytes = english_path.read_bytes(), bulgarian_path.read_bytes()
    english, bulgarian = json.loads(english_bytes), json.loads(bulgarian_bytes)
    if not isinstance(english, dict) or not isinstance(bulgarian, dict) or set(english) != set(bulgarian):
        raise ValueError('English/Bulgarian catalog key inventories must match')
    messages, literals, english_literals, nonliteral_keys, plurals = {}, {}, {}, {}, 0
    for key, source in sorted(english.items()):
        target = bulgarian[key]
        if isinstance(source, dict):
            if not isinstance(target, dict):
                raise ValueError('Invalid plural target: ' + key)
            plurals += 1
            continue  # API ErrorBody has no plural selector; never guess a form.
        if not isinstance(source, str) or not isinstance(target, str) or not target.strip():
            raise ValueError('Expected nonblank reviewed string: ' + key)
        if Counter(TOKEN.findall(source)) != Counter(TOKEN.findall(target)):
            raise ValueError('Placeholder mismatch: ' + key)
        if any(c in target for c in ('\0', '\ufffd')):
            raise ValueError('Invalid translation character: ' + key)
        messages[key] = target
        if source in english_literals and english_literals[source] != key:
            raise ValueError('Ambiguous exact English literal: ' + source)
        english_literals[source] = key
        if source != key:
            nonliteral_keys[key] = True
            if source in literals and literals[source] != key:
                raise ValueError('Ambiguous exact English literal: ' + source)
            literals[source] = key
    if GENERIC not in messages:
        raise ValueError('Reviewed generic error is required')
    payload = {'messages': messages, 'literals': literals, 'nonliteralKeys': nonliteral_keys}
    encoded = json.dumps(payload, ensure_ascii=True, sort_keys=True, separators=(',', ':'))
    delimiter = 'G2ERR' + hashlib.sha256(encoded.encode()).hexdigest()[:8]
    if ')' + delimiter + '"' in encoded:
        raise ValueError('C++ resource delimiter collision')
    sha = lambda value: hashlib.sha256(value).hexdigest()
    text = ('// Generated; do not edit. Source: authoritative en/bg online catalogs only.\n'
            '#pragma once\n#include <string_view>\nnamespace Online::ErrorCatalogData {\n'
            f'inline constexpr char EnglishSha256[] = "{sha(english_bytes)}";\n'
            f'inline constexpr char BulgarianSha256[] = "{sha(bulgarian_bytes)}";\n'
            f'inline constexpr unsigned MessageCount = {len(messages)};\n'
            f'inline constexpr unsigned ExcludedPluralCount = {plurals};\n'
            'inline constexpr std::string_view Chunks[] = {\n' +
            ''.join(f'R"{delimiter}({encoded[start:start + 8192]}){delimiter}",\n' for start in range(0, len(encoded), 8192)) +
            '};\n'
            '}\n')
    text.encode('ascii')  # MSVC, native and both Emscripten variants share bytes.
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.tmp')
    temporary.write_text(text, encoding='ascii')
    temporary.replace(output)
    return {'english_sha256': sha(english_bytes), 'bulgarian_sha256': sha(bulgarian_bytes),
            'message_count': len(messages), 'excluded_plural_count': plurals,
            'generated_bytes': len(text.encode('ascii'))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--english', type=Path, required=True)
    parser.add_argument('--bulgarian', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(generate(args.english, args.bulgarian, args.output), sort_keys=True))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
