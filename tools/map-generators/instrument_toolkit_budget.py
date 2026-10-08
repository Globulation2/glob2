#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Find native toolkit loops and vector indexing that need script budget guards.

Development-only libclang 18 tool. The default reports pending edits without
changing source; --write applies them. Review its diff and add allocation guards
manually: loop checkpoints alone do not bound a native helper's memory use.
"""
import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
CHECKPOINT = b" ::MapGeneration::generationCheckpoint(); "
EXAMPLE_GENERATORS = ("Forts", "EvenGround", "Swamp")


@dataclass(frozen=True)
class Edit:
    replacement: bytes
    consumed: int = 0


class Instrumentation:
    """Collect edits once, even when several translation units include a header."""

    def __init__(self, clang, relevant):
        self.clang = clang
        self.relevant = relevant
        self.sources = {}
        self.edits = {}
        self.visited_loops = set()

    def source(self, path):
        # Clang offsets count bytes, including non-ASCII comments. Keep all edits
        # byte-based rather than applying offsets to decoded Unicode text.
        if path not in self.sources:
            self.sources[path] = Path(path).read_bytes()
        return self.sources[path]

    def add_edit(self, path, offset, edit):
        edits = self.edits.setdefault(path, {})
        previous = edits.get(offset)
        if previous is not None and previous != edit:
            raise ValueError(f"Conflicting edits at {path}:{offset}")
        edits[offset] = edit

    def vector_index(self, cursor, path):
        args = list(cursor.get_arguments())
        if len(args) != 2:
            return
        receiver = args[0].type.get_canonical().spelling.removeprefix("const ")
        if not receiver.startswith("std::vector<"):
            return
        source = self.source(path)
        bracket = source.find(b"[", args[0].extent.end.offset, args[1].extent.start.offset + 1)
        end = cursor.extent.end.offset - 1
        if bracket >= 0 and end > bracket and source[end:end + 1] == b"]":
            self.add_edit(path, bracket, Edit(b".at(", 1))
            self.add_edit(path, end, Edit(b")", 1))

    def loop(self, cursor, path):
        kinds = self.clang.CursorKind
        children = list(cursor.get_children())
        if not children:
            return
        body = children[0] if cursor.kind == kinds.DO_STMT else children[-1]
        start, end = body.extent.start.offset, body.extent.end.offset
        key = (path, start, end)
        if key in self.visited_loops:
            return
        self.visited_loops.add(key)
        if body.kind == kinds.COMPOUND_STMT:
            statements = list(body.get_children())
            # Comments have no AST statements. Long leading comments must not
            # make an already instrumented loop acquire a second checkpoint.
            if statements and statements[0].kind == kinds.CALL_EXPR and statements[0].spelling == "generationCheckpoint":
                return
            self.add_edit(path, start + 1, Edit(CHECKPOINT))
        else:
            source = self.source(path)
            if source[end:end + 1] == b";":
                end += 1
            self.add_edit(path, start, Edit(b" {" + CHECKPOINT))
            # Nested single-statement loops share their final byte offset. Each
            # needs its own closing brace, rather than deduplicating that edit.
            edits = self.edits.setdefault(path, {})
            previous = edits.get(end, Edit(b""))
            if previous.consumed:
                raise ValueError(f"Conflicting loop boundary at {path}:{end}")
            edits[end] = Edit(previous.replacement + b" } ")

    def walk(self, cursor):
        path = str(cursor.location.file) if cursor.location.file else ""
        if path and not self.relevant(path):
            return
        kinds = self.clang.CursorKind
        if path:
            if cursor.kind == kinds.CALL_EXPR and cursor.spelling == "operator[]":
                self.vector_index(cursor, path)
            elif cursor.kind in (kinds.FOR_STMT, kinds.CXX_FOR_RANGE_STMT, kinds.WHILE_STMT, kinds.DO_STMT):
                self.loop(cursor, path)
        for child in cursor.get_children():
            self.walk(child)

    def changed_sources(self):
        result = {}
        for path, edits in self.edits.items():
            source = self.source(path)
            previous = len(source)
            for offset, edit in sorted(edits.items(), reverse=True):
                if offset + edit.consumed > previous:
                    raise ValueError(f"Overlapping edits at {path}:{offset}")
                source = source[:offset] + edit.replacement + source[offset + edit.consumed:]
                previous = offset
            if b'"GenerationWork.h"' not in source:
                if b"#pragma once" in source:
                    source = source.replace(b"#pragma once", b'#pragma once\n#include "GenerationWork.h"', 1)
                else:
                    source = b'#include "GenerationWork.h"\n' + source
            result[path] = source
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="Apply proposed edits; otherwise report only")
    parser.add_argument("--libclang", default=os.getenv("LIBCLANG_PATH", "/usr/lib/x86_64-linux-gnu/libclang-18.so.1"))
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / "scons"))
    from sources import INCLUDE_DIRECTORIES
    from clang import cindex
    cindex.Config.set_library_file(args.libclang)
    shared = ROOT / "src/map/generator/shared"
    generators = [ROOT / "src/map/generator/generators" / f"{name}Generator.cpp" for name in EXAMPLE_GENERATORS]
    paths = sorted(path for path in shared.rglob("*.cpp") if not path.name.endswith("Test.cpp")) + generators
    generator_paths = {str(path) for path in generators}

    def relevant(path):
        return ("/map/generator/shared/" in path and not path.endswith(("Test.h", "Test.cpp"))) or path in generator_paths

    flags = ["-std=c++20", "-I" + str(ROOT / "third_party/quickjs-ng"), "-I" + str(ROOT / "build/linux/client/release/include")]
    flags += ["-I" + str(ROOT / path) for path in INCLUDE_DIRECTORIES]
    if os.getenv("GLOB2_SDL3_PREFIX"):
        flags.append("-I" + os.environ["GLOB2_SDL3_PREFIX"] + "/include")
    instrumentation = Instrumentation(cindex, relevant)
    index = cindex.Index.create()
    for path in paths:
        unit = index.parse(str(path), args=flags)
        errors = [str(d) for d in unit.diagnostics if d.severity >= cindex.Diagnostic.Error]
        if errors:
            parser.exit(2, "\n".join(errors) + "\n")
        instrumentation.walk(unit.cursor)
    changed = instrumentation.changed_sources()
    for path, source in sorted(changed.items()):
        print(Path(path).relative_to(ROOT))
        if args.write:
            Path(path).write_bytes(source)
    print(f"{'Updated' if args.write else 'Pending'}: {len(changed)} files")
    return 0 if args.write or not changed else 1


if __name__ == "__main__":
    raise SystemExit(main())
