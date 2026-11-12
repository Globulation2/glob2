#!/usr/bin/env python3
"""Object-level link graph of a native build, read with nm.

Each object file (and each member of a static archive) is a node. An edge A -> B
means A references a symbol that B defines strongly, so linking A pulls in B.
Symbols defined only weakly (inline functions, template instances, vtables
without a key function) are usually emitted by every user and add no edge;
`--weak` also follows them to their first definer.

  python3 tools/link_graph.py scc                      # strongly connected components
  python3 tools/link_graph.py closure map/Map.o        # what linking Map.o needs
  python3 tools/link_graph.py path game/Game.o hud/GameGUI.o   # why, symbol by symbol
  python3 tools/link_graph.py closure map/Map.o --within map   # only within a group

Objects are named by their path under the build directory, without the leading
`src/` (`map/Map.o`, `libgag.a(Sprite.o)`); a name argument matches any node
ending with it. The default build directory is the one test/run_tests.py uses
(GLOB2_BUILD_DIR, else the newest native build); `--build-dir` overrides it.
`--within GROUP` restricts the graph to a buildsys/sources.py group (map, sim,
core, client, or several joined with +) plus libgag, libusl and third_party;
`closure --external` then lists what the closure needs from outside the group,
which is exactly what a module test binary must stub or the group must absorb.
"""
import argparse
import collections
import concurrent.futures
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'buildsys'))

STRONG = set('TDBRGSCAI')   # defined, strong (POSIX nm types; uppercase = global)
WEAK = set('WVu')           # weak or unique global definitions
ENTRY_SYMBOLS = {'main', '_main', 'SDL_main', '_SDL_main', 'WinMain'}


def nm_symbols(path):
    """{member: (defined_strong, defined_weak, undefined)} for an object or archive."""
    completed = subprocess.run(['nm', '-P', '-A', str(path)], capture_output=True, text=True)
    if completed.returncode:
        raise SystemExit(f'nm failed on {path}:\n{completed.stderr}')
    members = collections.defaultdict(lambda: (set(), set(), set()))
    for line in completed.stdout.splitlines():
        # "path[member]: symbol type value size" or "path: symbol type ..."
        head, _, rest = line.rpartition(': ')
        fields = rest.split()
        if len(fields) < 2:
            continue
        symbol, kind = fields[0], fields[1]
        match = re.match(r'.*\[(.+)\]$', head)
        member = match.group(1) if match else ''
        strong, weak, undefined = members[member]
        if kind == 'U':
            undefined.add(symbol)
        elif kind in STRONG:
            strong.add(symbol)
        elif kind in WEAK:
            weak.add(symbol)
    return members


def default_build_dir():
    from build_paths import native_build_directory
    return native_build_directory()


def cmake_objects(build_dir, include_tests):
    """(node name, path) of a CMake build's objects, named by source:
    client objects by their path under src/ ('map/Map.o'), test objects under 'test/'."""
    found = []
    for directory in sorted((build_dir / 'CMakeFiles').glob('*.dir')):
        target = directory.name[:-len('.dir')]
        test = target.startswith(('glob2_engine_tests', 'glob2_unit_tests', 'glob2_map_tests',
                                  'glob2_sim_tests', 'glob2_test_production'))
        if target not in ('glob2_client', 'glob2') and not (include_tests and test):
            continue
        for path in sorted(directory.rglob('*.o')):
            name = path.relative_to(directory).as_posix()
            name = name[:-len('.cpp.o')] + '.o' if name.endswith('.cpp.o') else name
            name = name[len('src/'):] if name.startswith('src/') else name
            found.append(('test/' + name if test else name, path))
    return found


def collect(build_dir, include_tests=False):
    """Nodes for every client object and the libgag/libusl archive members."""
    if (build_dir / 'CMakeFiles').is_dir():
        objects = cmake_objects(build_dir, include_tests)
        archives = [build_dir / name for name in ('libgag.a', 'libusl.a', 'libGlob2JavaScript.a')
                    if (build_dir / name).exists()]
        paths = [path for _, path in objects] + archives
        names = [name for name, _ in objects] + [None] * len(archives)
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            tables = list(pool.map(nm_symbols, paths))
        nodes = {}
        for path, name, members in zip(paths, names, tables):
            for member, table in members.items():
                member = member.replace('.cpp.o', '.o').replace('.c.o', '.o') if member else member
                nodes[f'{path.name}({member})' if member else name] = table
        return nodes
    paths = sorted(p for p in (build_dir / 'src').rglob('*.o'))
    paths += sorted((build_dir / 'libs').rglob('*.a'))
    paths += sorted((build_dir / 'third_party').rglob('*.o'))
    if include_tests:
        paths += sorted((build_dir / 'test').rglob('*.o'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        tables = list(pool.map(nm_symbols, paths))
    nodes = {}
    for path, members in zip(paths, tables):
        relative = path.relative_to(build_dir).as_posix()
        relative = relative[len('src/'):] if relative.startswith('src/') else relative
        for member, table in members.items():
            name = f'{path.name}({member})' if member else relative
            nodes[name] = table
    return nodes


class Graph:
    def __init__(self, nodes, weak=False):
        self.nodes = sorted(nodes)
        strong_owner, weak_owner = {}, {}
        # Production objects own a symbol before the test doubles that redefine it.
        # Program entry points (app/Glob2.o, harness mains) own nothing: each program
        # brings its own, so their globals (globalContainer) count as unresolved.
        self.entry_points = {name for name in self.nodes if nodes[name][0] & ENTRY_SYMBOLS}
        for name in sorted(self.nodes, key=lambda node: (node.startswith('test/'), node)):
            if name in self.entry_points:
                continue
            for symbol in nodes[name][0]:
                strong_owner.setdefault(symbol, name)
            for symbol in nodes[name][1]:
                weak_owner.setdefault(symbol, name)
        self.program_globals = {symbol for name in self.entry_points for symbol in nodes[name][0]}
        self.edges = {name: {} for name in self.nodes}   # target -> example symbol
        self.references = {name: {} for name in self.nodes}   # target -> every symbol
        self.unresolved = {name: set() for name in self.nodes}
        for name in self.nodes:
            strong, own_weak, undefined = nodes[name]
            for symbol in sorted(undefined):
                owner = strong_owner.get(symbol) or (weak_owner.get(symbol) if weak else None)
                if owner and owner != name:
                    self.edges[name].setdefault(owner, symbol)
                    self.references[name].setdefault(owner, []).append(symbol)
                elif symbol not in strong_owner and symbol not in weak_owner:
                    self.unresolved[name].add(symbol)

    def restrict(self, keep):
        restricted = Graph.__new__(Graph)
        restricted.nodes = [name for name in self.nodes if name in keep]
        restricted.edges = {name: {t: s for t, s in self.edges[name].items() if t in keep}
                            for name in restricted.nodes}
        restricted.references, restricted.unresolved = self.references, self.unresolved
        restricted.entry_points, restricted.program_globals = self.entry_points, self.program_globals
        restricted.outside = {name: {t: s for t, s in self.references[name].items() if t not in keep}
                              for name in restricted.nodes}
        return restricted

    def resolve(self, name):
        exact = [node for node in self.nodes if node == name]
        matches = exact or [node for node in self.nodes if node.endswith('/' + name) or node.endswith(name)]
        if len(matches) != 1:
            raise SystemExit(f'{name!r} matches {len(matches)} objects' + (': ' + ', '.join(matches[:10]) if matches else ''))
        return matches[0]

    def closure(self, roots):
        seen, stack = set(roots), list(roots)
        while stack:
            for target in self.edges[stack.pop()]:
                if target not in seen:
                    seen.add(target)
                    stack.append(target)
        return seen

    def path(self, source, target):
        previous, queue = {source: None}, collections.deque([source])
        while queue:
            node = queue.popleft()
            if node == target:
                chain = [node]
                while previous[chain[-1]] is not None:
                    chain.append(previous[chain[-1]])
                return chain[::-1]
            for successor in self.edges[node]:
                if successor not in previous:
                    previous[successor] = node
                    queue.append(successor)
        return None

    def sccs(self):
        """Tarjan's algorithm, iterative."""
        index, low, on_stack, stack, result = {}, {}, set(), [], []
        counter = 0
        for start in self.nodes:
            if start in index:
                continue
            work = [(start, iter(self.edges[start]))]
            index[start] = low[start] = counter
            counter += 1
            stack.append(start)
            on_stack.add(start)
            while work:
                node, successors = work[-1]
                advanced = False
                for successor in successors:
                    if successor not in index:
                        index[successor] = low[successor] = counter
                        counter += 1
                        stack.append(successor)
                        on_stack.add(successor)
                        work.append((successor, iter(self.edges[successor])))
                        advanced = True
                        break
                    if successor in on_stack:
                        low[node] = min(low[node], index[successor])
                if advanced:
                    continue
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[node])
                if low[node] == index[node]:
                    component = []
                    while True:
                        member = stack.pop()
                        on_stack.discard(member)
                        component.append(member)
                        if member == node:
                            break
                    result.append(sorted(component))
        return sorted(result, key=lambda c: (-len(c), c))


def demangle(symbols):
    completed = subprocess.run(['c++filt'], input='\n'.join(symbols), capture_output=True, text=True)
    return completed.stdout.splitlines() if completed.returncode == 0 else list(symbols)


def area(node):
    """A short grouping key: the source directory, or the archive."""
    if '(' in node:
        return node.split('(', 1)[0]
    parts = node.split('/')
    return '/'.join(parts[:2]) if len(parts) > 2 else (parts[0] if len(parts) > 1 else '.')


def summarize(members, limit):
    by_area = collections.Counter(area(member) for member in members)
    return ', '.join(f'{name} {count}' for name, count in by_area.most_common(limit))


def group_objects(spec):
    """Objects of one or more '+'-joined buildsys/sources.py groups (map, sim, core, client)."""
    import sources
    groups = {'map': sources.MAP_SOURCES, 'sim': sources.SIM_SOURCES, 'core': sources.CORE_SOURCES,
              'client': sources.CLIENT_SOURCES}
    keep = set()
    for name in spec.split('+'):
        if name not in groups:
            raise SystemExit('--within takes ' + ', '.join(sorted(groups)) + ', joined with +')
        keep |= {source[:-len('.cpp')] + '.o' for source in groups[name]}
    return keep


def is_library(node):
    """libgag, libusl and vendored third-party objects: linked by every binary."""
    return '(' in node or node.startswith('third_party/')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=None)
    parser.add_argument('--weak', action='store_true', help='also follow weak-only definitions')
    parser.add_argument('--tests', action='store_true', help='include test/ objects as nodes')
    parser.add_argument('--within', metavar='GROUP', help='restrict nodes to a buildsys/sources.py group')
    parser.add_argument('--with', dest='extra', action='append', default=[], metavar='OBJECT',
                        help='with --within: also keep this object (try a group change before making it)')
    commands = parser.add_subparsers(dest='command', required=True)
    scc = commands.add_parser('scc', help='strongly connected components, largest first')
    scc.add_argument('--min', type=int, default=2, help='smallest component to print (default 2)')
    scc.add_argument('--members', action='store_true', help='list every member')
    closure = commands.add_parser('closure', help='objects transitively needed by the given objects')
    closure.add_argument('objects', nargs='+')
    closure.add_argument('--members', action='store_true', help='list every member')
    closure.add_argument('--external', action='store_true',
                         help='with --within: the symbols the closure needs from outside the group, '
                              'by defining object (what a stub or a move must supply)')
    closure.add_argument('--unresolved', action='store_true',
                         help='symbols the closure references that no client object defines '
                              '(system libraries, and variables a program entry point owns)')
    path = commands.add_parser('path', help='a shortest reference chain between two objects')
    path.add_argument('source')
    path.add_argument('target')
    args = parser.parse_args(argv)

    build_dir = (args.build_dir or default_build_dir()).resolve()
    if not (build_dir / 'src').is_dir():
        raise SystemExit(f'{build_dir} has no src/ objects; build it first')
    graph = Graph(collect(build_dir, args.tests), args.weak)
    if args.within:
        keep = group_objects(args.within) | set(args.extra)
        graph = graph.restrict({node for node in graph.nodes if node in keep or is_library(node)})
    print(f'[link_graph] {build_dir}: {len(graph.nodes)} objects, '
          f'{sum(len(e) for e in graph.edges.values())} edges', flush=True)

    if args.command == 'scc':
        components = [c for c in graph.sccs() if len(c) >= args.min]
        print(f'{len(components)} components of {args.min}+ objects')
        for component in components:
            print(f'{len(component):5d}  {summarize(component, 12)}')
            if args.members:
                for member in component:
                    print('         ' + member)
    elif args.command == 'closure':
        roots = [graph.resolve(name) for name in args.objects]
        reached = graph.closure(roots)
        print(f'{len(reached)} objects reachable from {", ".join(roots)}')
        print('  ' + summarize(reached, 40))
        if args.members:
            for member in sorted(reached):
                print('    ' + member)
        if args.external:
            if not args.within:
                raise SystemExit('--external needs --within GROUP')
            needed = collections.defaultdict(lambda: collections.defaultdict(set))
            for member in reached:
                for owner, symbols in graph.outside[member].items():
                    for symbol in symbols:
                        needed[owner][symbol].add(member)
            print(f'{len(needed)} objects outside {args.within} are referenced:')
            for owner in sorted(needed, key=lambda o: (-len(needed[o]), o)):
                symbols = sorted(needed[owner])
                print(f'  {owner} ({len(symbols)} symbols)')
                for symbol, readable in zip(symbols, demangle(symbols)):
                    users = sorted(needed[owner][symbol])
                    print(f'      {readable}  <- {", ".join(users[:3])}' + (' ...' if len(users) > 3 else ''))
        if args.unresolved:
            missing = sorted({symbol for member in reached for symbol in graph.unresolved[member]})
            # C++ names and the globals a program entry point defines (globalContainer);
            # other C symbols are the system and SDL libraries.
            readable = [name for name, mangled in zip(demangle(missing), missing)
                        if mangled in graph.program_globals or mangled.startswith('_Z') and '::' in name
                        and not name.split(' for ')[-1].startswith(('std::', '__cxxabiv1::', '__gnu_cxx::'))
                        and not name.startswith(('std::', '__gnu_cxx::'))]
            print(f'{len(readable)} unresolved C++ symbols and program globals (defined by no client object, or only by an entry point):')
            for name in readable:
                print('      ' + name)
    else:
        source, target = graph.resolve(args.source), graph.resolve(args.target)
        chain = graph.path(source, target)
        if chain is None:
            print(f'{target} is not reachable from {source}')
            return 1
        symbols = [graph.edges[a][b] for a, b in zip(chain, chain[1:])]
        for node, symbol in zip(chain, demangle(symbols) + ['']):
            print(node + (f'\n    -> {symbol}' if symbol else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
