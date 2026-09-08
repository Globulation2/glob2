# ADR 004: explicit coroutine jobs for nested loading

Status: incremental implementation; loading is not yet fully latency-bounded.

Game loading is an ordered parser: teams precede the map, the map precedes
players, and scripts follow integrity checks. Streams, section guards, and
intermediate objects must survive between stages. `CooperativeTask` uses C++20
coroutine frames to retain those objects across deliberate checkpoints. It has
no browser APIs, threads, wall clock, or Asyncify dependency. Existing native
callers can drain the same task through a synchronous adapter.

Loading screens use the measured cooperative slice described below. Awaited children report checkpoints
to that root; completing a child resumes the parent until its next checkpoint.
Destroying the root destroys all suspended child frames. Exceptions propagate to
the root result. Job lifetimes must be shorter than the game and stream they
borrow. A task and its game must not be used concurrently or advanced reentrantly.

This differs from retaining arbitrary UI call stacks through Asyncify: all
suspension points are explicit in parser code and resource ownership follows
ordinary C++ lifetimes. The tradeoff is a small coroutine scheduler and a C++20
compiler requirement (the existing native and Emscripten targets already use
C++20). Native and Wasm builds and lifecycle tests gate this choice. It should be
reviewed alongside the alternative of hand-written parser state machines.

The editor entry flow owns a staging editor and its loading task in
`EditorLoadScreen`. Only successful completion transfers the editor to
`MapEditorScreen`. Cancellation/failure destroys partial data and restores the
previous global RNG state. Map cleanup releases each allocation independently;
it cannot assume every gradient array has been constructed. Error messages are
owned screens rather than modal calls inside the loader.

Current checkpoints cover game stages, teams and players, chunks of 512 terrain
cells, gradient seeding, and individual propagation sweeps. Legacy fertility loading uses the bounded
fertility job. Remaining work includes subdividing large team/player parsing,
building-specific gradients, stream decompression, scripts, and allocation
work; a checkpoint count is not evidence of a maximum frame time. In-session game reload callers still drain synchronously and need owned loading
flows; startup and editor replacement are described below.
Map generation also uses owned cooperative jobs (ADR 005). Editor sprites remain owned by the toolkit
cache so destroying a staging editor cannot invalidate another editor's sprite.

Tests cover nested suspension/completion, exception propagation, cancellation
cleanup, partial gradient allocation cleanup, RNG restoration, and equality of
scheduled and synchronous loading. Browser tests exercise cancellation/restart
with real menu input. The coroutine lifecycle tests also pass under AddressSanitizer. Existing simulation
checksums remain regression gates.

## Single-player startup ownership

`GameGUI` and `Engine` now expose cooperative startup tasks that await the shared
parser. Headers and filenames are copied into the job so a completed selection
screen can be destroyed safely. Stream ownership follows the suspended GUI loader.
Custom games, saved games, replays, and campaign missions use `GameLoadScreen`,
which owns the engine until initialization succeeds. The campaign screen remains
alive while its mission loads and runs.

Cancellation destroys the task before the engine, clears pending replay/network
initialization, and restores the prior RNG state. This screen is for startup
only, with no active session; replacing a live session requires a separate
transaction because the legacy replay globals are shared. Engine file/replay
writers are initialized at the final step, with no intervening UI checkpoint.
Loader errors and campaign-save errors return to owned message screens.

Synchronous adapters remain for command-line, native network, and in-session
reload callers. Replay indexing, AI initialization, individual parser stages,
serialization, and initial music loading are not yet fully subdivided. Passing
startup cancellation tests does not certify a maximum loading frame duration.

## Replacing a map inside the editor

The editor's load selector now emits a replacement request. `MapEditorScreen`
keeps the current editor alive while an `EditorLoadScreen` builds a separate one.
Only successful completion swaps ownership. Cancellation and failure retain the
old map and its unsaved-edit state; failures display an owned error screen.
The existing loader's RNG rollback applies to this transaction too.

Opening a child clears held keys, modifiers, active drags, and edge-scroll input
without changing window focus or the camera. This prevents controls released in
a child screen from becoming stuck when its parent resumes. Button events still
supply their own hit-test coordinates.

Native tests compare the retained map checksum/RNG after cancellation and a
missing-file failure, then use input to verify the unsaved-edit prompt remains.
Browser tests cancel a replacement, resume the original map, then successfully
replace it and verify that it is unmodified. In-session game replacement is still
separate work because engine replay/session globals require different ownership.

## Gradients during loading

The rebase preserves upstream's lazy weighted resource and area gradients.
Loading no longer eagerly allocates and propagates those fields. The shared
8-bit AI helper gradient retains a cooperative adapter with checkpoints every
16 rows of each forward/backward sweep; simulation callers drain it synchronously.
The task borrows its map and buffers, which must remain private while suspended.

An independent priority-queue relaxation oracle verifies exact gradient values
for an empty source set, toroidal wrapping, barriers with gaps, and multiple
source strengths. Synchronous and scheduled sweeps match this oracle. Destroying
an interrupted sweep and restarting from its partial monotonic result converges
to the same values. Startup tests retain their existing callback completion limit;
bounded batching avoids a browser callback for every new gradient checkpoint.

The time budget is checked at explicit checkpoints, not a preemption boundary. Memory
allocation, team construction, and remaining parser/terrain operations still need
latency qualification. Gradient changes also run the native speed/replay suite
because simulation callers share the synchronous implementation.

## Team setup during generation

`Game::addTeamTask` awaits `Map::addTeamTask`, preserving team masks, colors,
header count, prestige limits, and script initialization order. Upstream's lazy
gradients mean this nested task can now finish without suspending.

The asynchronous API is for a privately owned preparation game. Cancellation
discards that game and restores RNG state. Tests exercise partial team parsing
and editor preparation cancellation; they do not require obsolete eager-gradient
checkpoints. Construction and allocation still need loading latency qualification.

## Host work budgets and clock injection

`CooperativeSlice` is a host pacing policy shared by game loading, editor loading,
and generation. It advances a root until completion, four milliseconds of steady
clock time, or 64 checkpoints, whichever is observed first. Cheap steps can be
batched without paying browser callback and repaint overhead for each small piece
of work. The checkpoint cap also protects against a frozen or coarse clock.

The clock is injectable. Unit tests charge a fake clock for each step and verify
elapsed-time stopping, a fresh budget on the next callback, completion without an
extra callback, the checkpoint cap, and an oversized step stopping immediately
at its next checkpoint. Lifecycle fixtures inject a frozen clock with an explicit
eight-checkpoint cap so cancellation depth is reproducible; production uses the
steady clock and the normal budgets. No test-only behavior switches are present.

This does not guarantee that a frame lasts four milliseconds: a single unfinished
parser/constructor operation can exceed the budget, and input/rendering cost is
outside it. Such operations still require subdivision and measured latency gates.
The clock controls pacing only; job results, simulation, and synchronous adapters
remain independent of wall time.

Saved-team parsing now yields between batches of unit slots, building slots, and
cross-reference resolution. The preparation game owns each team before parsing
starts, so cancellation destroys partially loaded objects with their owner.
The synchronous adapter drains the same parser and preserves serialization order.
Native tests cancel in each stage, including a fixture containing real units and
buildings, and check scheduled-load continuation against the session checksum.
