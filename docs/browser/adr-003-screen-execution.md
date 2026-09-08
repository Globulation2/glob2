# ADR 003: explicit screen execution phases

Status: first runtime migration step; screen-stack conversion remains pending.

`Screen` now exposes `beginExecution`, `updateExecution`,
`handleExecutionEvent`, `drawExecution`, and `finishExecution`. The host supplies
input and timer values. These methods do not poll events or wait. This makes
screen lifecycle behavior testable without depending on wall-clock timing.

`execute()` remains a compatibility host: it polls SDL, coalesces motion and
window events, drives those phases, and uses the application host's wait.
Existing menus therefore share the new execution path while their callers are
migrated incrementally. Legacy timer-before-input ordering is retained.

Completion stops subsequent phase dispatch. Creation callbacks can complete a
screen immediately, and destruction callbacks run once when the host finishes
it. Double execution and finishing a running screen are rejected. A completed
screen can be started again. Application quit remains a distinct result.

The regression harness uses SDL's dummy software display and explicit timer
values. It covers phase separation, completion from creation/input/timer,
ignored input after completion, quit propagation, reuse, and compatibility.
Run `scons release=1 screen-test` followed by
`build/<toolchain>/client/release/libgag/src/ScreenExecutionHarness`.

An owning screen stack now supplies deferred transitions and completion
callbacks for the campaign selector flow, as described below. Overlay modal
loops, engine scheduling, resumable jobs, and removal of Asyncify are still
required. Calling a legacy child `execute()` from a callback is still blocking;
this API extraction does not claim that all screen callbacks are resumable.

## Owning stack and first migrated flow

`ScreenStack` owns screens with `unique_ptr`. Hosts submit an SDL event batch
and a timer sample to `frame`; the stack itself does not poll or sleep. Pushes
are queued and applied at a frame boundary. Requesting a child suspends further
parent input immediately, so the opening input cannot activate the child.
Completion callbacks can inspect the completed screen before it is destroyed;
its parent stays alive. A pending child is cancelled if its parent completes
before admission. Application quit unwinds owned screens without invoking
continuations that could open another flow. Recursive frames are rejected.

The campaign new/load selector now uses this stack. Selection cancellation
returns to the retained parent; successful selection queues the campaign menu.
`ScreenStack::execute` is a transitional polling host for the current desktop
and Asyncify browser callers. Campaign mission execution still uses the legacy
engine loop, and other menu families have not yet migrated. This change does
not remove Asyncify or claim callback-safe mission loading.
