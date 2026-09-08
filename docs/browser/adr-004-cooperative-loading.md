# ADR 004: explicit coroutine jobs for nested loading

Status: incremental implementation; loading is not yet fully latency-bounded.

Game loading is an ordered parser: teams precede the map, the map precedes
players, and scripts follow integrity checks. Streams, section guards, and
intermediate objects must survive between stages. `CooperativeTask` uses C++20
coroutine frames to retain those objects across deliberate checkpoints. It has
no browser APIs, threads, wall clock, or Asyncify dependency. Existing native
callers can drain the same task through a synchronous adapter.

A host advances the root once per callback. Awaited children report checkpoints
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
cells, and individual gradient builds. Legacy fertility loading uses the bounded
fertility job. Remaining work includes subdividing large team/player parsing,
individual gradient algorithms, stream decompression, scripts, and allocation
work; a checkpoint count is not evidence of a maximum frame time. Engine and
in-editor load callers still drain synchronously and need owned loading flows.
Map generation remains synchronous. Editor sprites remain owned by the toolkit
cache so destroying a staging editor cannot invalidate another editor's sprite.

Tests cover nested suspension/completion, exception propagation, cancellation
cleanup, partial gradient allocation cleanup, RNG restoration, and equality of
scheduled and synchronous loading. Browser tests exercise cancellation/restart
with real menu input. The coroutine lifecycle tests also pass under AddressSanitizer. Existing simulation
checksums remain regression gates.
