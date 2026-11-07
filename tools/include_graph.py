#!/usr/bin/env python3
"""Static include graph of the repository's quoted and angled project includes.

The graph resolves each `#include` the way the compiler would for a client
translation unit: the including file's directory first (quoted includes only),
then `INCLUDE_DIRECTORIES` from `scons/sources.py`, then the test and library
directories the SConscripts add. Includes that resolve to no repository file
(system, SDL, Boost, generated headers) are ignored. Preprocessor conditions
are not evaluated, so the graph over-approximates slightly; it matched
`clang++ -M` within 0.5% for the widest headers when this tool was written.

Typical use:

    python3 tools/include_graph.py top            # widest headers by client TU fan-in
    python3 tools/include_graph.py fanin src/app/Version.h src/map/Map.h
    python3 tools/include_graph.py why src/app/Version.h   # direct includers, by reach
    python3 tools/include_graph.py median         # median repo headers reached per TU
    python3 tools/include_graph.py blast --since origin/master~200   # rebuild share of recent commits

`check_header_layering.py` imports this module for its fan-in budget.
"""
from __future__ import annotations

import argparse
import functools
import json
import os
import re
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.M)
SOURCE_SUFFIXES = {'.cpp', '.cc', '.c', '.mm'}
HEADER_SUFFIXES = {'.h', '.hpp', '.hh', '.inl', '.ipp'}
SKIP_DIRECTORIES = {'build', 'artifacts', '.git', 'node_modules', 'third_party', 'services', 'platforms', 'docs'}
# Directories the SConscripts add to CPPPATH beyond INCLUDE_DIRECTORIES.
EXTRA_DIRECTORIES = ('test', 'test/support', 'libs/gag/src', 'test/third_party', 'third_party/quickjs-ng', 'src/relay')


def _sources_module(root: Path):
    sys.path.insert(0, str(root / 'scons'))
    try:
        import sources  # noqa: PLC0415
    finally:
        sys.path.pop(0)
    return sources


class IncludeGraph:
    def __init__(self, root: Path = ROOT):
        self.root = root
        sources = _sources_module(root)
        self.search = [root / d for d in (*sources.INCLUDE_DIRECTORIES, *EXTRA_DIRECTORIES)]
        self.client_units = sorted({f'src/{s}' for s in sources.CLIENT_SOURCES} |
                                   {f'libs/gag/src/{s}' for s in sources.GAG_SOURCES} |
                                   {f'libs/usl/src/{s}' for s in sources.USL_SOURCES})
        self.client_units = [u for u in self.client_units if (root / u).is_file()]
        self._edges: dict[str, tuple[str, ...]] = {}

    # --- graph construction -------------------------------------------------
    def all_units(self) -> list[str]:
        """Every C/C++ source under src/, libs/ and test/."""
        found = []
        for top in ('src', 'libs', 'test'):
            for directory, names, files in os.walk(self.root / top):
                names[:] = [n for n in names if n not in SKIP_DIRECTORIES]
                for name in files:
                    if Path(name).suffix in SOURCE_SUFFIXES:
                        found.append(str(Path(directory, name).relative_to(self.root)))
        return sorted(found)

    def all_headers(self) -> list[str]:
        found = []
        for top in ('src', 'libs', 'test'):
            for directory, names, files in os.walk(self.root / top):
                names[:] = [n for n in names if n not in SKIP_DIRECTORIES]
                for name in files:
                    if Path(name).suffix in HEADER_SUFFIXES:
                        found.append(str(Path(directory, name).relative_to(self.root)))
        return sorted(found)

    @functools.lru_cache(maxsize=None)
    def _resolve(self, directory: str, kind: bytes, name: str) -> str | None:
        candidates = ([self.root / directory] if kind == b'"' else []) + self.search
        for base in candidates:
            path = base / name
            if path.is_file():
                try:
                    return os.path.normpath(str(path.relative_to(self.root)))
                except ValueError:
                    return None
        return None

    def includes(self, path: str) -> tuple[str, ...]:
        """Repository files `path` includes directly."""
        cached = self._edges.get(path)
        if cached is not None:
            return cached
        try:
            text = (self.root / path).read_bytes()
        except OSError:
            text = b''
        directory = str(Path(path).parent)
        result = []
        for kind, name in INCLUDE.findall(text):
            resolved = self._resolve(directory, kind, name.decode('utf-8', 'replace'))
            if resolved and resolved != path and resolved not in result:
                result.append(resolved)
        self._edges[path] = tuple(result)
        return self._edges[path]

    def reach(self, unit: str) -> set[str]:
        """Every repository file a translation unit pulls in (excluding itself)."""
        seen: set[str] = set()
        stack = [unit]
        while stack:
            for child in self.includes(stack.pop()):
                if child not in seen:
                    seen.add(child)
                    stack.append(child)
        return seen

    def fanin(self, units: list[str] | None = None) -> dict[str, int]:
        """Number of translation units reaching each header."""
        counts: dict[str, int] = {}
        for unit in units if units is not None else self.client_units:
            for header in self.reach(unit):
                counts[header] = counts.get(header, 0) + 1
        return counts

    def includers(self, header: str, units: list[str] | None = None) -> dict[str, int]:
        """Direct includers of `header` with the number of TUs each one brings it to."""
        result: dict[str, int] = {}
        for unit in units if units is not None else self.client_units:
            reached = self.reach(unit) | {unit}
            if header not in reached:
                continue
            for node in reached:
                if header in self.includes(node):
                    result[node] = result.get(node, 0) + 1
        return result


def report_blast(graph: IncludeGraph, units: list[str], since: str, until: str) -> int:
    """Fraction of TUs each commit's changes reach, on today's include graph.

    An approximation of the historical rebuild cost: it uses the current graph,
    so it measures how expensive those edits would be now."""
    import subprocess
    reaching: dict[str, set[str]] = {}
    for unit in units:
        for header in graph.reach(unit) | {unit}:
            reaching.setdefault(header, set()).add(unit)
    log = subprocess.check_output(['git', '-C', str(graph.root), 'log', '--first-parent', '--format=%H', f'{since}..{until}'],
                                  text=True).split()
    shares = []
    for sha in log:
        changed = subprocess.check_output(['git', '-C', str(graph.root), 'diff-tree', '--no-commit-id', '--name-only', '-r', sha],
                                          text=True).split()
        hit = set().union(*(reaching.get(path, set()) for path in changed)) if changed else set()
        if any(Path(path).suffix in SOURCE_SUFFIXES | HEADER_SUFFIXES for path in changed):
            shares.append(len(hit) / len(units))
    if not shares:
        print('no code commits in range')
        return 0
    shares.sort()
    def pct(q):
        return 100 * shares[min(len(shares) - 1, int(q * len(shares)))]
    print(f'{len(shares)} code commits; rebuild share median {pct(0.5):.1f}%, p75 {pct(0.75):.1f}%, p90 {pct(0.9):.1f}%; '
          f'over 50%: {100 * sum(s > 0.5 for s in shares) / len(shares):.1f}% of commits; '
          f'mean {100 * sum(shares) / len(shares):.1f}%')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--all-units', action='store_true', help='count every source under src/, libs/, test/ instead of client TUs')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--root', type=Path, default=ROOT, help='repository checkout to analyse (default: this one)')
    sub = parser.add_subparsers(dest='command', required=True)
    top = sub.add_parser('top', help='widest headers')
    top.add_argument('-n', type=int, default=30)
    fan = sub.add_parser('fanin', help='fan-in of named headers')
    fan.add_argument('headers', nargs='+')
    why = sub.add_parser('why', help='direct includers of a header ranked by the TUs they bring it to')
    why.add_argument('header')
    why.add_argument('-n', type=int, default=20)
    sub.add_parser('median', help='median repository headers reached per TU')
    blast = sub.add_parser('blast', help='share of client TUs each recent first-parent commit would recompile')
    blast.add_argument('--since', required=True, help='revision to start after (e.g. origin/master~200)')
    blast.add_argument('--until', default='HEAD')
    args = parser.parse_args(argv)

    graph = IncludeGraph(args.root.resolve())
    units = graph.all_units() if args.all_units else graph.client_units
    if args.command == 'median':
        counts = [len([h for h in graph.reach(u) if Path(h).suffix in HEADER_SUFFIXES]) for u in units]
        payload = {'units': len(units), 'median': statistics.median(counts),
                   'headers_reaching_half': sum(1 for v in graph.fanin(units).values() if v * 2 >= len(units))}
        print(json.dumps(payload) if args.json else
              f"{payload['units']} TUs; median repo headers reached {payload['median']}; "
              f"headers reaching >=50% of TUs: {payload['headers_reaching_half']}")
        return 0
    if args.command == 'blast':
        return report_blast(graph, units, args.since, args.until)
    if args.command == 'why':
        rows = sorted(graph.includers(os.path.normpath(args.header), units).items(), key=lambda r: -r[1])[:args.n]
        for name, count in rows:
            print(f'{count:6d}  {name}')
        return 0
    counts = graph.fanin(units)
    if args.command == 'top':
        rows = sorted(((h, c) for h, c in counts.items() if Path(h).suffix in HEADER_SUFFIXES), key=lambda r: -r[1])[:args.n]
    else:
        rows = [(h, counts.get(os.path.normpath(h), 0)) for h in args.headers]
    if args.json:
        print(json.dumps({'units': len(units), 'fanin': dict(rows)}, indent=1))
    else:
        print(f'{len(units)} translation units')
        for name, count in rows:
            print(f'{count:6d}  {100 * count / max(1, len(units)):5.1f}%  {name}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
