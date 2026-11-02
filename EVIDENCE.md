# Rendering border validation

PR: https://github.com/Globulation2/glob2/pull/1343
Final source:659ae9c9560f4759283ee0edd24c5924ebac09fd. Integrated base:726b37f573d2b1401033b5536562c7ee92b4050d.

Scope is seven rendering/test/documentation files. Independent read-only review found no actionable issues. No movement, AI, save, replay, network, simulation version or generator changes. The sim-version contract check passed without updating the simulation record.

macOS27.2 arm64, AppleClang21: routine dev_fast build (-O0,-g1), not a performance measurement. Linux x86_64 GCC13: ordinary release=1 build with pinned SDL3 dependencies. Linux0be5 is token-identical to final659; the final delta only corrects one closing-brace indentation. Final Linux recompile qualification is recorded separately.

Visual tests check ordinary versus cold and warmed cached rendering, terrain edits, zoom1x and3x, and wrapped viewports. Every captured backend is required to be the actual requested backend. Linux GL/Vulkan coverage uses Mesa llvmpipe software adapters and does not establish hardware GPU rendering. macOS tests exercise software and Apple M3 OpenGL; SDL-GPU/Metal is unavailable due to missing offline shader assets. Browser, Windows and mobile execution coverage is not claimed by these local runs; hosted checks are reported in the PR.

The input save is the same frozen191 format initial state for each4096-tick run, with compute-threads8 and checksum telemetry. Input hash:adb162448c5a51ad29f3339579fa4485870aa030243dd4e3d5e3eee0b3449af2. Exact commands, build identities, raw traces, XML reports, backend identities and PNGs are retained alongside this document. No goldens have been regenerated for the rendering change.

Linux: all5 visual cases and43 terrain-material cases pass, with zero skips; committed multiplayer match golden passes. Resource-composition golden fails identically on the untouched current master726 and candidate, with unchanged random-state hashes. The existing fixture is preserved. Base reproduction XML and logs are included. Final source review and diff whitespace checks pass.

Final qualification: both native platforms completed50 selected unique cases:49 passed,1 pre-existing resource golden failure,0 skipped. Final macOS versus Linux comparison matched all4096 world and4097 snapshot rows exactly. Linux candidate also matches current master726, with final659 game byte-identical to the compared integrated executable. Linux final659 visual/material/golden tests were rerun after relinking. Focused final screenshots are published; broader gallery artifacts remain local. Hosted CI is still pending at preparation time.
