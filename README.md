# PR 1193 merge verification and limits

Final PR head: 05d9027bfe4ea1114cceab6f073d29b94cd03761, integrated with master 9ea290745. Merge conflict resolved by retaining both IntrusiveEntityList.h and TeamCapacity.h in Team.h.

Completed on final integrated source: GCC 13.3 API/assertion/include-order/assembly probes; 32768 valid GID round trips in each assertion mode, ten expected assertion aborts, two historical accepted negative building cases, null reference defaults, three include orders; four MinGW header probes. All twelve moved function bodies match new baseline after whitespace/equivalent constant qualifiers. New-base optimized Linux client build completed.

Earlier revision 09833a668 against e7572da6b: full optimized Linux client and both full test binaries built. Unit cases: 1096 passed, 1 unrelated ResourceRegistry size-limit error, 89 skipped. Focused engine cases: 191 passed, 3 BuildingStock fixture errors; those three errors reproduced identically on baseline. The custom-resource golden mismatch also reproduced identically on baseline. CLI smoke: 14 passed, one unchanged completion expectation failed; maintainer requested ignoring it without baseline reproduction, and none was performed.

Baseline/candidate per-tick checksums, saved-state bytes and replay bytes matched at 1 and 8 compute participants from the retained tick-24000 checkpoint to tick 25153. Current-format checkpoint reload matched all 577 resumed ticks. Matching trace files and result JSON are retained here. The original format-164 checkpoint resaves as 165 on that earlier base; its fixed header-only checksum XOR was isolated by the current-format test.

Environment and exact build flags are retained in environment.json and build logs. Earlier release used -O3 -g, retain_symbols=1, linker=auto, no LTO/PCH/unity/dev_fast. Refreshed integration used identical optimized baseline/candidate flags with CXXFLAGS=-g0; the candidate rebuild and final runtime/profile reruns were NOT finished.

At the maintainer's explicit request to merge immediately, remaining local validation was stopped. Final integrated release build/runtime qualification and the three paired Callgrind measurements remain unfinished. No measured speedup or cross-platform simulation equivalence is claimed. Earlier runtime passes are not presented as final-revision passes.

macOS attempt on original 09833a668 failed at the unrelated asset dependency install: Python 3.15 has no pinned Pillow 12.2.0 wheel. https://github.com/Globulation2/glob2/actions/runs/38818777342 . Windows runtime, macOS runtime, browsers, mobile and full hosted matrix are omitted. Hosted PR checks are cheap contracts only.

Maintainer explicitly authorized immediate merge after reviewing these work-in-progress limits in the task. No product dependency, arithmetic, assertion, state-field or serialization change was introduced by the helper-inlining diff. Existing simulation/save/replay version changes from PR 1188 are inherited.
