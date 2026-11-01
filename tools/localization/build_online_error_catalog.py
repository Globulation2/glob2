#!/usr/bin/env python3
"""Embed reviewed Bulgarian and Thai strings for the native error presenter.

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


def generate(english_path, bulgarian_path, output, thai_path=None, *, _thai=False):
    english_bytes, bulgarian_bytes = english_path.read_bytes(), bulgarian_path.read_bytes()
    english, bulgarian = json.loads(english_bytes), json.loads(bulgarian_bytes)
    if not isinstance(english, dict) or not isinstance(bulgarian, dict) or set(english) != set(bulgarian):
        raise ValueError('English/Bulgarian catalog key inventories must match')
    messages, literals, english_literals, nonliteral_keys, plurals = {}, {}, {}, {}, 0
    ambiguous_literals = set()
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
            ambiguous_literals.add(source)
        english_literals[source] = key
        if source != key:
            nonliteral_keys[key] = True
            literals[source] = key
    if GENERIC not in messages:
        raise ValueError('Reviewed generic error is required')
    # Context-qualified keys can share an English display value. Structured
    # keys remain exact; an unqualified legacy literal must not pick a context.
    for source in ambiguous_literals:
        literals.pop(source, None)
    payload = {'messages': messages, 'literals': literals, 'nonliteralKeys': nonliteral_keys,
               'ambiguousLiterals': {literal: True for literal in sorted(ambiguous_literals)}}
    encoded = json.dumps(payload, ensure_ascii=True, sort_keys=True, separators=(',', ':'))
    delimiter = 'G2ERR' + hashlib.sha256(encoded.encode()).hexdigest()[:8]
    if ')' + delimiter + '"' in encoded:
        raise ValueError('C++ resource delimiter collision')
    sha = lambda value: hashlib.sha256(value).hexdigest()
    namespace = 'ThaiErrorCatalogData' if _thai else 'ErrorCatalogData'
    target_name = 'Thai' if _thai else 'Bulgarian'
    text = ('// Generated; do not edit. Source: authoritative reviewed online catalogs.\n'
            f'#pragma once\n#include <string_view>\nnamespace Online::{namespace} {{\n'
            f'inline constexpr char EnglishSha256[] = "{sha(english_bytes)}";\n'
            f'inline constexpr char {target_name}Sha256[] = "{sha(bulgarian_bytes)}";\n'
            f'inline constexpr unsigned MessageCount = {len(messages)};\n'
            f'inline constexpr unsigned ExcludedPluralCount = {plurals};\n'
            'inline constexpr std::string_view Chunks[] = {\n' +
            ''.join(f'R"{delimiter}({encoded[start:start + 8192]}){delimiter}",\n' for start in range(0, len(encoded), 8192)) +
            '};\n'
            '}\n')
    result = {'english_sha256': sha(english_bytes), 'bulgarian_sha256': sha(bulgarian_bytes),
              'message_count': len(messages), 'excluded_plural_count': plurals,
              'generated_bytes': len(text.encode('ascii'))}
    if thai_path is not None:
        # Keep each locale resource independent and hash-bound. Publish the
        # combined header only after both resources validate successfully.
        thai_output = output.with_suffix(output.suffix + '.thai')
        try:
            thai_result = generate(english_path, thai_path, thai_output, _thai=True)
            text += thai_output.read_text(encoding='ascii')
        finally:
            thai_output.unlink(missing_ok=True)
        result['thai_sha256'] = thai_result['bulgarian_sha256']
        result['generated_bytes'] = len(text.encode('ascii'))
    text.encode('ascii')  # MSVC, native and both Emscripten variants share bytes.
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.tmp')
    temporary.write_text(text, encoding='ascii')
    temporary.replace(output)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--english', type=Path, required=True)
    parser.add_argument('--bulgarian', type=Path, required=True)
    parser.add_argument('--thai', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(generate(args.english, args.bulgarian, args.output, args.thai), sort_keys=True))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
