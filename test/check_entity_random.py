#!/usr/bin/env python3
"""Production simulation, setup, generation and AI must never consume an implicit RNG stream."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = re.compile(r'\b(?:SyncRandScope|SyncRandRequirement)\b|\b(?:syncRand|syncRandEngine|bindRandom|setSyncRandSeed|getSyncRandState|setSyncRandState|setRandomSyncRandSeed|rand|srand|random_shuffle)\s*\(')
COMMENTS_AND_STRINGS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)


def violations(root=ROOT):
    files = list((root / 'src').rglob('*.cpp')) + list((root / 'src').rglob('*.h'))
    for path in files:
        if path.name.endswith(('Test.cpp', 'Harness.cpp', 'Benchmark.cpp', 'Checks.h', 'Contracts.h', 'Fixtures.cpp')):
            continue
        if path.relative_to(root).as_posix() in ('src/common/Utilities.cpp', 'src/common/Utilities.h'):
            continue # Historical test diagnostics, no production callers.
        source = COMMENTS_AND_STRINGS.sub(lambda match: '\n' * match.group().count('\n'), path.read_text())
        # Compatibility-only test binding is declared alongside historical state.
        if path.relative_to(root).as_posix() == 'src/game/Game.h':
            source = re.sub(r'SyncRandScope bindRandom\(\).*', '', source)
        for match in FORBIDDEN.finditer(source):
            yield f'{path.relative_to(root)}:{source.count(chr(10), 0, match.start()) + 1}: implicit shared RNG'


if __name__ == '__main__':
    found = list(violations())
    for error in found:
        print(error)
    if not found:
        print('Private RNG ownership contract passed')
    raise SystemExit(bool(found))
