# Code conventions

Local coding and tooling conventions for contributors. Contribution policy lives in [AGENTS.md](../../AGENTS.md).

## Local conventions

Preserve other contributors' work; use an isolated checkout when work overlaps.
Use PascalCase C++ filenames (not snake_case) and `#pragma once`. The map generator tree
(`src/map/generator/`, its tests and the lobby's landscape picker) is kept in the style
`.clang-format` at the repository root describes: tabs, Allman braces, 100 columns. Run
`clang-format -i` on the files you touch there (`pip install clang-format` gives a current
binary), and `clang-format --dry-run -Werror` over the tree to check it. Case-only renames
on macOS need an intermediate filename, e.g. `git mv Foo.cpp temp.cpp` then
`git mv temp.cpp foo.cpp`. Keep comments terse and about the current code; put change
history and rationale in commit messages; omit tombstone or “moved to” comments.
Diagnostics use `std::cerr`; there is no logging facility to target.
Windows headers define `near`, `far` and `small` as macros, so never name an identifier
after one: mingw expands `int near[3]` to `int [3]`, which then fails as a structured
binding declaration, and the error points at the syntax rather than at the macro.

For unused-include cleanup, generate `compile_commands.json` and use
`tools/remove-unused-includes.py`; do not apply blind bulk fixes. Rebuild client,
relay and affected tests, then verify behavior-preserving simulation changes as above.
