# Script interpreter lifetimes during reload

## Problem

In-game save/replay loading reuses a game engine after simulation has run. The
browser regression found a use-after-free in `MapScriptUSL::compileCode`, when
looking up the existing `gui` bridge constant. Initial loads and recompilation
before simulation did not reproduce it.

The interpreter's root and prototype are owned outside the GC heap, so sweeping
did not clear their marks. Later collections skipped root traversal and freed
still-referenced constants. Active thread frames were not marked, and marking
did not traverse all prototype, closure and bytecode references. Native method
tables were static across interpreters but contained heap-owned methods.

## Decision

Keep the existing interpreter and mark/sweep design. Reset external root marks
for each collection; trace live frames, prototypes, closure environments,
locals, constants and bytecode-created prototypes. Cache native method tables
per heap and retain that cache during collection. Destroy all remaining heap
values when their interpreter is destroyed. No gameplay or browser-specific
conditionals control collection.

This supports repeated game loads and independent validation/editor interpreters
without retaining an entire interpreter forever or borrowing another one's
method table. Script execution rules and serialized formats are unchanged.

## Verification

`EngineSessionHarness` checks repeated collection of roots, reclamation of dead
values, preservation of active stacks and pending bytecode constants, independent
native method tables, and final heap cleanup. The focused GC block also passed
AddressSanitizer and UndefinedBehaviorSanitizer. Browser tests exercise repeated
in-game save loads, replay reloads and failure recovery after real simulation.
Native/browser matches check that the shared runtime still agrees on checksums.

The public MapScriptUSL header now forward-declares its owned interpreter. USL and
interpreter definitions are included only by MapScriptUSL.cpp. This prevents
unrelated game-header consumers in headless servers from instantiating prototype
code or acquiring a link dependency on the script engine. GCC exposed this
boundary issue where the macOS compiler had discarded unused definitions.
