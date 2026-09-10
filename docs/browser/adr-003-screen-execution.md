# ADR 003: scheduled screen and session execution

Status: accepted.

Interactive browser execution uses explicit phases and owned navigation. No
browser callback retains a suspended C++ call stack.

## Screen lifecycle

`Screen` exposes `beginExecution`, `updateExecution`, `handleExecutionEvent`,
`drawExecution`, and `finishExecution`. The host supplies input batches and timer
values. Completion stops further dispatch, and destruction runs once after the
owner has observed the result.

`ScreenStack` owns screens with `unique_ptr`. Push, completion, and replacement
requests are deferred to frame boundaries. A parent stops receiving input as soon
as it requests a child, remains alive while that child runs, and receives the
result before the child is destroyed. Application quit unwinds the stack without
running continuations that could reopen navigation.

`Application` owns the stack and the top-level flows. Its `frame(tick, events)`
and `delay(now)` methods are the common native/browser update interface. Native
`ApplicationHost::run` polls SDL around that interface; the browser host schedules
one callback at a time.

The synchronous `Screen::execute` compatibility host remains for native-only
callers. Browser entry points do not call it.

## Game sessions

`GameSessionScreen` owns an `Engine` and drives its begin, step, draw, delay, and
finish operations. `Engine::stepSession` consumes host-supplied input without
polling SDL. Gameplay owns held-key and modifier state derived from those events;
focus loss clears held input, scrolling, and drag state.

Campaigns, tutorials, custom games, replays, and multiplayer matches all use the
same game-session screen. The engine remains alive while the end-game screen is
open so statistics and replay export cannot outlive game state. In-game load and
replay requests finish the current session, transfer ownership through a
`GameLoadScreen`, and either resume the retained session or return an error to its
parent.

Native command-line and headless drivers retain synchronous engine adapters over
the same session operations.

## Cooperative work

Browser-reachable map parsing, game initialization, map generation, and fertility
calculation run as bounded cooperative tasks. Each task owns temporary state,
publishes results only after successful completion, and can be cancelled by
releasing it. Failed or cancelled work leaves the caller's prior state intact.

The map editor itself is a scheduled `MapEditorScreen`. Quit confirmation,
generation, loading, fertility calculation, and save results are owned child
screens, so cancelling a dialog resumes the same editor and its unsaved map.
Campaign-editor entry drafts are owned by their completion callbacks and outlive
screens that borrow them.

## YOG and LAN ownership

YOG login, registration, lobby tabs, room setup, map transfer, and matches are
owned by the application stack. Network callbacks record state; transitions are
requested only after the current client update returns. A pending match launch
queues cooperative game loading and then the shared `GameSessionScreen`.

Orders received during loading remain queued until the engine attaches. Engine
teardown detaches the borrowed network pointer. Transfer-screen destruction
cancels active transfers, and lobby teardown breaks client/game ownership cycles.
Desktop LAN discovery, admission, lobby, and match execution follow the same
stack ownership; discovery resumes after a join session returns.

## Overlay ownership

Features that retain their own drawing surface, such as end-game replay saving,
drive their overlay's events, timers, drawing, persistence result, resizing, and
destruction from the parent screen. They do not enter nested polling loops or
store a captured background image.

## Compatibility boundary

Legacy synchronous screen, overlay, message-box, and engine APIs still serve
native call sites that are outside the browser application. The Emscripten link
does not enable Asyncify, and a browser call to `ApplicationHost::wait` fails
rather than blocking or spinning. Tests verify that browser-reachable flows stay
inside the scheduled stack.

The retained compatibility surface is deliberately finite:

- `Screen::execute`, `Glob2Screen::execute`, and `Glob2TabScreen::execute` run a
  native polling loop for older native callers.
- `ScreenStack::execute` is the native adapter around the same frame-driven
  stack used by the browser.
- `OverlayScreen::execute`, `OverlayScreen::executeModal`, and the message-box
  modal helper remain for native-only dialogs. Browser-reachable game, editor,
  replay, and persistence overlays are driven by their owning screen instead.
- `Engine::run` and `Engine::runOneGameSession` remain for command-line and
  native synchronous entry points. `GameSessionScreen` owns interactive browser
  sessions and their end screens.
- `ApplicationHost::wait` maps to `SDL_Delay` only in the native host. It is a
  hard error in the browser host.

This list is the migration inventory. New interactive flows must use
`ScreenStack` and host-supplied frames; they must not call one of these adapters
from a scheduled callback. Removing an adapter requires migrating its listed
native caller and retaining the native lifecycle tests. Other methods named
`execute`, such as server administrator commands, dispatch commands and are not
screen execution APIs.

## Verification

The native screen and session harnesses cover lifecycle ordering, deferred
navigation, cancellation, quit propagation, deterministic simulation under
varied callback schedules, input/focus cleanup, editor ownership, load failure,
and repeated interpreter use. Browser tests exercise the same flows with real
input across Chromium, Firefox, and WebKit, including repeated loads, editor
cancellation, replay saving, YOG navigation, match startup, and clean shutdown.

Interpreter lifetime fixes discovered by repeated loading are recorded in
[ADR 007](adr-007-script-lifetimes.md). Cooperative generation decisions are
recorded in [ADR 004](adr-004-cooperative-loading.md) and
[ADR 005](adr-005-generation-randomness.md).
