#!/usr/bin/env python3
"""Keep the header include graph layered, cheap to rebuild, and free of version constants.

Rules (all computed from the static include graph in tools/include_graph.py, so
this runs in seconds without a build):

1. Layers. Every header under src/ and libs/ has a rank from
   test/header_layers.json (the most specific directory prefix wins). A header
   may include only headers of the same or a lower rank. Existing violations
   are listed in test/header_layering_allowlist.txt; new ones fail.
2. Simulation headers (the `simulation` prefixes in header_layers.json) never
   include rendering, HUD or UI headers, the full SDL umbrella, GAGSys.h or
   SDLGraphicContext.h. Existing violations share the allowlist above.
3. No header gives a parameter or member a version constant as its default
   (`= VERSION_MINOR`, `= SIM_REVISION`, `= FILE_FORMAT_VERSION_*`). Such a
   default makes every includer recompile whenever the version moves; declare
   an overload in the header and define it in the .cpp instead. No allowlist.
4. Directory cycles. Directory-level include edges (depth 1 and 2 below src/)
   that sit on a cycle are listed in test/header_cycles_allowlist.txt; a new
   cycle edge fails.
5. Fan-in budget. test/header_fanin_budget.json caps how many client
   translation units may reach each listed header.

Allowlists and budgets only ratchet down: after a PR removes violations or
lowers fan-in, run with --write to delete the stale allowlist lines and lower
the budgets. --write never adds a violation or raises a budget; edit the file
by hand (and say why in the PR) if one must grow.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from include_graph import HEADER_SUFFIXES, IncludeGraph  # noqa: E402

TEST = ROOT / 'test'
LAYERS = TEST / 'header_layers.json'
LAYER_ALLOWLIST = TEST / 'header_layering_allowlist.txt'
CYCLE_ALLOWLIST = TEST / 'header_cycles_allowlist.txt'
BUDGET = TEST / 'header_fanin_budget.json'

VERSION_DEFAULT = re.compile(
    r'(?<![=!<>])=\s*(VERSION_[A-Z_]+|SIM_REVISION|MINIMUM_VERSION_MINOR|REPLAY_MINIMUM_VERSION_MINOR|'
    r'FILE_FORMAT_VERSION_[A-Z0-9_]+|NET_PROTOCOL_VERSION)\s*[,)};]')
SYSTEM_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*<([^>]+)>', re.M)
FORBIDDEN_SYSTEM_FOR_SIM = {'SDL3/SDL.h'}
FORBIDDEN_HEADERS_FOR_SIM = {'libs/gag/include/GAGSys.h', 'libs/gag/include/SDLGraphicContext.h'}


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return [line.strip() for line in path.read_text().splitlines() if line.strip() and not line.startswith('#')]


def write_lines(path: Path, header: str, lines: list[str]) -> None:
    path.write_text(header + ''.join(f'{line}\n' for line in sorted(set(lines))))


class Layers:
    def __init__(self, config: dict):
        self.ranks = sorted(config['ranks'].items(), key=lambda item: -len(item[0]))
        self.simulation = config['simulation']
        self.presentation = config['presentation']

    @staticmethod
    def _under(path: str, prefix: str) -> bool:
        return path == prefix or path.startswith(prefix.rstrip('/') + '/')

    def rank(self, path: str) -> int | None:
        parts = path.split('/')
        for prefix, rank in self.ranks:
            if '*' in prefix:
                # "src/*/types" style: one wildcard component.
                pattern = prefix.split('/')
                if len(parts) > len(pattern) and all(p == '*' or p == q for p, q in zip(pattern, parts)):
                    return rank
            elif self._under(path, prefix):
                return rank
        return None

    def is_simulation(self, path: str) -> bool:
        # Presentation subdirectories (src/map/editor, src/unit/render, ...) sit above the
        # simulation even though they live inside a simulation directory.
        rank = self.rank(path)
        return (any(self._under(path, p) for p in self.simulation) and not self.is_presentation(path)
                and rank is not None and rank <= 6)

    def is_presentation(self, path: str) -> bool:
        return any(self._under(path, p) for p in self.presentation)


def project_headers(graph: IncludeGraph) -> list[str]:
    return [h for h in graph.all_headers() if h.startswith(('src/', 'libs/'))]


def layer_violations(graph: IncludeGraph, layers: Layers) -> list[str]:
    found = []
    for header in project_headers(graph):
        own = layers.rank(header)
        if own is None:
            continue
        simulation = layers.is_simulation(header)
        for child in graph.includes(header):
            child_rank = layers.rank(child)
            if child_rank is not None and child_rank > own:
                found.append(f'{header} -> {child}')
            elif simulation and (layers.is_presentation(child) or child in FORBIDDEN_HEADERS_FOR_SIM):
                found.append(f'{header} -> {child}')
        if simulation:
            text = (graph.root / header).read_text(errors='replace')
            for name in SYSTEM_INCLUDE.findall(text):
                if name in FORBIDDEN_SYSTEM_FOR_SIM:
                    found.append(f'{header} -> <{name}>')
    return sorted(set(found))


def version_defaults(graph: IncludeGraph) -> list[str]:
    found = []
    for header in project_headers(graph) + [h for h in graph.all_headers() if h.startswith('test/')]:
        text = (graph.root / header).read_text(errors='replace')
        for number, line in enumerate(text.splitlines(), 1):
            code = line.split('//', 1)[0]
            if '#define' in code or 'constexpr' in code:
                continue
            if VERSION_DEFAULT.search(code):
                found.append(f'{header}:{number}: {line.strip()}')
    return found


def directory(path: str, depth: int) -> str | None:
    parts = path.split('/')
    if parts[0] != 'src' or len(parts) < 2 + depth:
        return None
    return '/'.join(parts[:1 + depth])


def cycle_edges(graph: IncludeGraph) -> list[str]:
    edges: set[str] = set()
    for depth in (1, 2):
        adjacency: dict[str, set[str]] = {}
        for header in project_headers(graph):
            source = directory(header, depth)
            if source is None:
                continue
            for child in graph.includes(header):
                target = directory(child, depth)
                if target and target != source:
                    adjacency.setdefault(source, set()).add(target)
                    adjacency.setdefault(target, set())
        component = strongly_connected(adjacency)
        for source, targets in adjacency.items():
            for target in targets:
                if component[source] == component[target]:
                    edges.add(f'{source} -> {target}')
    return sorted(edges)


def strongly_connected(adjacency: dict[str, set[str]]) -> dict[str, int]:
    """Tarjan's algorithm, iterative."""
    index: dict[str, int] = {}
    low: dict[str, int] = {}
    on_stack: set[str] = set()
    stack: list[str] = []
    component: dict[str, int] = {}
    counter = 0
    for root in sorted(adjacency):
        if root in index:
            continue
        work = [(root, iter(sorted(adjacency[root])))]
        index[root] = low[root] = counter
        counter += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            node, children = work[-1]
            advanced = False
            for child in children:
                if child not in index:
                    index[child] = low[child] = counter
                    counter += 1
                    stack.append(child)
                    on_stack.add(child)
                    work.append((child, iter(sorted(adjacency[child]))))
                    advanced = True
                    break
                if child in on_stack:
                    low[node] = min(low[node], index[child])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[node])
            if low[node] == index[node]:
                while True:
                    member = stack.pop()
                    on_stack.discard(member)
                    component[member] = index[node]
                    if member == node:
                        break
    return component


def check(write: bool = False, seed: bool = False, root: Path = ROOT) -> list[str]:
    graph = IncludeGraph(root)
    layers = Layers(json.loads(LAYERS.read_text()))
    errors: list[str] = []

    for line in version_defaults(graph):
        errors.append(f'version constant used as a default in a header (declare an overload, define it in the .cpp): {line}')

    allow_header = ('# Existing layering violations, "includer -> included". Generated by\n'
                    '# test/check_header_layering.py --write; delete lines, never add them.\n')
    cycle_header = ('# Directory-level include edges that sit on a cycle today. Generated by\n'
                    '# test/check_header_layering.py --write; delete lines, never add them.\n')
    for name, current, path, header in (
            ('layering', layer_violations(graph, layers), LAYER_ALLOWLIST, allow_header),
            ('directory cycle', cycle_edges(graph), CYCLE_ALLOWLIST, cycle_header)):
        allowed = set(read_lines(path))
        new = [edge for edge in current if edge not in allowed]
        if seed:
            write_lines(path, header, current)
            continue
        for edge in new:
            errors.append(f'new {name} edge: {edge} (see {path.relative_to(ROOT)})')
        stale = allowed - set(current)
        if write and stale:
            write_lines(path, header, sorted(allowed & set(current)))
        elif stale:
            errors.append(f'{len(stale)} {name} allowlist entries no longer occur; run '
                          f'python3 test/check_header_layering.py --write to drop them')

    budget = json.loads(BUDGET.read_text()) if BUDGET.exists() else {}
    if budget:
        counts = graph.fanin()
        changed = False
        for header, limit in budget.items():
            if header.startswith('_'):
                continue
            count = counts.get(header, 0)
            if count > limit:
                errors.append(f'{header} reaches {count} client translation units, budget {limit}; '
                              f'run python3 tools/include_graph.py why {header}')
            elif (write or seed) and count < limit:
                budget[header] = count
                changed = True
            elif count < limit:
                print(f'note: {header} fan-in fell to {count} (budget {limit}); --write lowers the budget')
        if changed:
            BUDGET.write_text(json.dumps(budget, indent=2) + '\n')
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--write', action='store_true', help='drop resolved allowlist lines and lower budgets')
    parser.add_argument('--seed', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    errors = check(write=args.write, seed=args.seed)
    for error in errors:
        print(error)
    if errors:
        print(f'{len(errors)} header layering problem(s)')
        return 1
    print('header layering: ok')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
