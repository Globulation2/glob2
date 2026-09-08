# ADR 002: explicit host calls during lifecycle migration

Status: transitional implementation; Asyncify removal is not complete.

The browser experiment forcibly redefined `SDL_Delay` in every translation unit
and slept inside `GraphicContext::nextFrame`. This changed unrelated networking
waits and hid application scheduling inside drawing. It also placed JavaScript
diagnostics in the engine and UI classes.

`ApplicationHost` now owns the transitional wait and diagnostic contract.
Desktop implements waits using SDL. Browser implements waits using Asyncify,
including a yield when an application frame is already late. UI loops call it
explicitly. Rendering does not wait. Browser interop is compiled only from the
browser platform directory. Static dependency tests enforce that boundary.

The browser publishes a versioned, read-only `glob2Diagnostics` interface:
screen, simulation tick, engine frame count, pause state, render dimensions,
storage restore/write activity, audio state, save names and save digests.
Tests drive the real UI and use these observations to wait for outcomes.
This interface cannot issue orders, advance ticks, alter saves, or navigate
menus. The legacy Emscripten `Module` is still exposed by the current shell.

This is not the final callback-based application lifecycle. Blocking screen
and modal loops still need replacement with explicit screen-stack transitions;
loading/map generation still need resumable, cancellable jobs. The supported
target must remove Asyncify and have both hosts drive the same update API.
See the [Emscripten execution model](https://emscripten.org/docs/porting/emscripten-runtime-environment.html).

The initial maintained suite uses [Playwright projects](https://playwright.dev/docs/test-configuration)
for Chromium, Firefox and WebKit. It does not establish Safari/Edge release
coverage, checkpoint correctness, or durable-write failure handling.
