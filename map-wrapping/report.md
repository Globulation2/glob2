# Map wrapping verification — PR #975

Tested candidate `7bed8f543140ceb78139fdf35a36fe1dceb6be49` against matching baseline `7de7d1553163d1a70738e2a313f2d6955918aebd` on therig, Linux x86-64, GCC 13.4.0. Normal release -O3, profile=0, -g2; compiler wrapper removes link stripping, without -pg. ELF debug sections and symbol tables are retained. Exact commands, compiler and binary hashes are in metadata.json, command JSON files and build logs; ldd.txt records linked dependencies. Audit instrumentation and symbol wrapper are included separately from production source.

## Correctness

- 921 unit cases passed, including signed/mixed-type extrema, arbitrary-grid fallbacks and unchanged Torus layout. A million-input arithmetic check also passed under UBSan.
- 598 selected engine cases passed after correcting three environment failures; 45 cases skipped. Original run: 595 passed, three failed. One EngineSession viewport case required the runner-selected display driver; two TerrainMaterials cases required the current baseline tileset catalog instead of a stale exported runtime catalog. All three passed on the recorded rerun. Original XML counts assertion failures, not failed cases. Raw failure logs and exact corrected configuration are retained.
- Ten rendering/editor cases covered. Original run: nine passed, one software renderer initialization error from forcing unavailable X11. Corrected rerun passed software and both OpenGL variants. No source changes were needed for these environment corrections.
- All 564 committed generator golden rows passed with zero differences.
- Five archived scenarios (sparse, established, hiring, dense, combat), each with one and four executor participants, continued 4,096 ticks from identical saves. All ten candidate runs have byte-identical per-tick detailed checksum traces, replays and final compressed saves against baseline. Fixture hashes and complete setup are in fixtures.json; commands, results and hash comparisons are under continuations/. Loading and teardown are outside the continuation window; scheduling delays are unchanged.
- Selected engine coverage includes legacy/binary/text save continuity, replay/network acceptance, live-list/hiring/fetch and golden-match contracts. Static simulation-version validation passed. No SIM_REVISION, recipe or golden updates.

Selection follows the broad native wrapping changes across AI, simulation, grids, rendering/editor and generators; exact case inventories are included. This was a focused engine selection, not the full engine suite. The maintainer requested local-only validation: Mac/ARM64, GCC 15, Windows and browser execution are omitted. Master was fetched; subsequent changes to the browser Studio fixture and a platform migration test do not overlap native source/build inputs, so no unrelated rebase was performed.

## Mechanism and limits

Optimized Cabino::Gradient::getHeight disassembly changes from 23 static instructions with two idiv instructions to 15 instructions with two AND operations and no division. Both disassemblies and machine-readable counts are included. These are static code counts, not dynamic whole-simulation instruction counts. No overall CPU or wall-time speedup is claimed; no new paired performance, Callgrind, allocation or cross-platform campaign was run for this broad pass.

Signed remainder semantics and C++ operand promotions are preserved. Fully normalized coordinates use existing helpers; arbitrary dimensions retain fallback behavior. Torus retains its two-integer layout to preserve script memory-budget accounting. Iteration order, RNG calls, scheduling, save formats and intended gameplay feel are unchanged.

## Downloadable raw evidence

[Continuation outputs](https://github.com/Globulation2/glob2/releases/tag/evidence-map-wrapping-975) contain baseline checksum traces, replays and final saves, plus candidate verification hashes. Candidate bytes are identical, so duplicate multi-gigabyte traces are omitted. [Archived input saves](https://github.com/Globulation2/glob2/releases/tag/evidence-serial-loop-963) provide the original fixtures. Production binaries are not uploaded; their hashes, build commands, source patch and instrumentation permit reproduction.
