# ADR 002: platform host boundary

Status: accepted.

The browser port originally replaced `SDL_Delay` across the program and waited
inside `GraphicContext::nextFrame`. That coupled scheduling to presentation and
also changed unrelated network waits.

Platform services now live behind `GAGCore::ApplicationHost`. The shared
application supplies a `Loop` with `frame` and `delay`; the desktop host polls
SDL until that loop completes, while the browser host schedules one callback at
a time and returns to JavaScript after every frame. Rendering does not wait or
own application scheduling.

The same boundary supplies viewport and visibility changes, file selection,
export, durable-storage requests, and optional diagnostics. Browser interop is
implemented under `browser/`; shared game, AI, UI, and renderer code does not
include Emscripten APIs. Static dependency tests enforce that boundary.

`glob2Diagnostics` is a versioned, read-only browser test interface. Its
`controls` map lists the visible screens' and dialogs' interactive controls by
key with logical-pixel bounds, so browser tests click real controls instead of
mirroring layout arithmetic. It reports
screen, simulation, rendering, persistence, audio, save, multiplayer, and custom-game readiness state.
It cannot issue orders, advance simulation, alter files, or navigate menus.

Legacy synchronous `Screen::execute` and
engine adapters remain for native command-line and desktop call sites. The web
entry points use `Application`, `ScreenStack`, and cooperative jobs and are
tested to ensure the linked runtime contains no Asyncify instrumentation.

## Consequences

- Browser callbacks always return to the event loop.
- Native behavior can retain synchronous compatibility hosts.
- New browser-reachable work must expose a scheduled screen or cooperative job.
- Browser-specific storage, input, and diagnostics remain isolated from shared
  simulation code.

See [ADR 003](adr-003-screen-execution.md) for screen and session ownership.
