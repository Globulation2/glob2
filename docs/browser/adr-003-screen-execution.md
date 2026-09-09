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
and Asyncify browser callers. Campaign mission execution now uses `GameSessionScreen`, described below;
other menu families have not yet migrated. This change does
not remove Asyncify or claim callback-safe mission loading.


## Incremental engine sessions

The engine exposes begin, step, draw, delay, and finish session operations.
Hosts supply monotonic millisecond samples and decide when to schedule the next
step. Delay queries never sleep or advance the simulation. Native `run()` drives
these same operations. The regression harness compares simulation checksums
under regular and delayed callback schedules and checks invalid lifecycle calls.

This is a session boundary, not yet the complete application scheduler:
`GameGUI::step(events, now)` consumes host-supplied input, but can still open
legacy modal dialogs. Its no-argument compatibility wrapper polls SDL. Finishing a
session can still synchronously load a requested save. Presentation preparation and end-game screen creation are shared with the
owned game-session screen. Music loading is still synchronous. These remaining call stacks must migrate
before a callback-only browser host can replace Asyncify.


## Ordered gameplay input

Gameplay owns held-key/modifier state derived from delivered events. Focus loss
clears held keys, mouse dragging, and edge scrolling. Returning focus requires
new input. Building previews and placement use those processed modifiers too.
Mouse buttons update state from their events; motion is dispatched before a
following button or focus event so an old motion cannot arrive after release.

`Engine::stepSession(now, events)` buffers input until its GUI cadence and never
consumes the host's event queue. The compatibility overload collects SDL events.
The native session harness plants a sentinel in SDL's queue to check this
boundary; the gameplay regression checks held-key scrolling and focus cleanup
with supplied timer samples. Browser visibility pause/resume, full menu input
migration, and nonblocking dialogs remain separate required work.


## Campaign session ownership

Campaign and tutorial menus now queue an owned `GameSessionScreen` after map
initialization. It drives the engine's incremental session API and exposes the
engine's requested delay to the common stack host. It retains the engine while
an end-game screen is on top, so statistics and replay export do not outlive
their game data. Returning from that screen completes the game screen and
refreshes/saves the retained campaign menu. Stack shutdown also saves campaign
progress while suppressing navigation continuations.

The legacy `Engine::run` shares presentation preparation and end-screen creation
with this path. Screen execution hooks are virtual so game presentation does
not run the menu renderer or translate input coordinates twice. The native
session harness checks the same fixture through this ownership path, and the
browser suite exercises two tutorial start/quit/end-screen/return cycles.

Map initialization, music loading, requested-save loading, and campaign save
error dialogs are still synchronous. Custom games, replays, editor, and network
menu flows still need to adopt the same ownership path before the browser host
can shed Asyncify.


## Custom games and load/replay navigation

`SinglePlayerFlow` owns navigation alongside the screen stack. It queues custom
setup or save/replay selection, initializes an engine from the selection, and
queues the same `GameSessionScreen` used by campaigns. Finishing a custom game
returns to fresh custom setup, preserving desktop behavior. Command-line
replays share this session ownership path. The old blocking no-argument
`Engine::initCustom` and `initLoadGame` methods are removed; engine initialization
accepts the selected map/player headers or filename directly.

Custom options and AI descriptions are child screens whose parent remains
alive, including the game-header references edited by the options screen.
Actual map/replay loading and in-session load requests remain synchronous and
must become resumable jobs. Editor/network flows and the outer main-menu loop
remain migration work; browser support still depends on Asyncify.


## Shared application host loop

`Application` owns the screen stack and navigation across menus and single-player
flows. Its `frame(tick, events)` and `delay(now)` are the common native/browser
update interface. Returning from a flow recreates the main menu, including
translated labels after settings changes. The old outer menu switch loop and
unused static main-menu execution entry point are removed.

Native `ApplicationHost::run` polls SDL and drives that interface until completion.
The browser implementation schedules one callback with `emscripten_async_call`
and queues its successor only when it returns. This also avoids concurrent frames
while a remaining legacy callback suspends through Asyncify. The host releases
all application state before its completion callback destroys global resources;
main does not report a premature browser exit just because scheduling returned.

The native host harness verifies completion/destruction ordering. Browser tests
cover settings/credits return and application exit, alongside gameplay flows.
Editor/network internals, loaders, and some dialogs remain blocking. The browser
build still uses Asyncify for those paths; scheduled outer execution is not a
claim that the complete runtime migration is finished.

## Editor navigation and borrowed draft lifetime

Editor setup, campaign selection, campaign editing, and campaign-map entry
editing now queue child screens. Newly added entries use a draft owned by the
completion callback; accepting the entry appends it to the campaign and displays
its edited name. Existing entries borrow from the retained parent campaign.
The stack destroys a completed/cancelled screen before releasing its completion
callback, so captured resources outlive any screen that borrows them. The native
harness checks normal completion, active cancellation, and cancellation before
admission.

At this stage, the map editor's own run loop, generation/loading, and save-error
message boxes remained synchronous; the next section records the loop migration. Failed map loading now returns without entering the editor
with invalid map data. The browser regression adds and reopens a campaign entry,
then cancels back through the owning parents.

## Incremental map editor

`MapEditorScreen` owns a loaded/generated `MapEdit`. The editor accepts supplied
input, advances editor state/timers, and draws through separate methods; its
old polling/sleeping run methods are removed. The common host applies its 33 ms
cadence. Held keys come from processed events, and focus loss clears scrolling
and active drags.

Quitting a modified map queues `MessageScreen`, an in-game decision with an
explicit caption-index result. Cancel resumes the retained editor, discard
finishes it, and save opens its existing save interface. No editor call stack is
suspended for this decision. The native fixture exercises cancel/discard; browser
checks generate a uniform map and navigate both decisions through actual input.
Generation, parsing, save I/O, fertility calculation, and remaining nested error
or script dialogs still require resumable/asynchronous migration.

## Resumable fertility work

Fertility calculation now exposes a platform-independent `Job`. Seeding,
resource reachability, and the weighting kernel all advance under an explicit
operation budget. Temporary distances and output belong to the job. The map must
remain alive and unchanged during the job; only a ready job may publish results.
Cancellation is destruction of the job, leaving the map unchanged. Final commit
copies the staged values in one pass so rendering never sees partial results.

The editor owns a `FertilityScreen` child while calculating overlays or preparing
a map save. Its host schedules bounded work and continues accepting cancellation.
Canceling either the save selector or calculation preserves unsaved edits and
cancels any pending quit. A failed file-open is reported through an owned message.
The synchronous adapter remains for old map-format loading; serialization and
browser durability are separate work still to migrate.

A frozen pre-migration algorithm in the native harness checks exact equality at
three operation budgets, including single-operation calls. Additional assertions
cover monotonic progress, rejected premature commit, cancellation, and publication
only after commit. Browser tests cover save cancellation, completed map writes,
and reload persistence using real controls and read-only file digests.

### YOG session ownership

YOG login and registration use the application screen stack. The login-accepted
listener requests a transition; `YOGLoginScreen::onTimer` performs it after the
client update returns. The existing listener list does not permit removing the
current listener during notification, so transitions must respect that boundary.

`YOGSessionScreen` owns the lobby/options/maps tabs by value. The lobby owns its
multiplayer game tab. Child map selectors, join progress, transfer screens and
notices use stack completions, with their parent kept alive below them. Tab
cleanup tolerates a group already removed on completion, including empty tabs.
Transfer screen destruction cancels active transfers. Match execution and the
multiplayer settings dialog remain legacy calls; this ownership migration does
not remove their Asyncify dependency.

### Scheduled YOG matches

A server start message records a pending launch; it does not run a simulation
inside socket dispatch. After the client update returns, the YOG game tab queues
`GameLoadScreen` with `Engine::initMultiplayerTask`, then `GameSessionScreen`.
The same task backs the synchronous initialization wrapper. Initialization rejects
an absent local player before indexing the game header, and map-load failure
returns through an in-game notice. Cancelled loading leaves the match.

Router orders stay in the connection queue from start admission until the engine
is attached. This preserves orders from a faster peer while cooperative loading
is incomplete. Engine teardown detaches the borrowed network-engine pointer.
The read-only browser diagnostic exposes `screenClass` separately from its
human-readable `screen` state so scheduled execution can be verified during play. `roomCanStart` reports the
room Start control’s readiness; observing a network frame alone does not prove
that the UI has consumed it.

YOG match settings also use a stack child and completion. LAN setup and the
headless peer retain explicit synchronous hosts during migration; they consume
pending launches after network update too. The optional stack argument on the
shared multiplayer tab is transitional, not the supported platform end state.


### LAN cleanup

Desktop LAN hosting and discovery/join navigation now share the application
screen stack. `LANSessionScreen` advances connection, login and room admission
on timer updates, with a 10-second deadline per handshake stage. Cancel and failed
admission close the connection; errors use scheduled notices. The lobby owns its
multiplayer tab and breaks client/game ownership cycles when it closes. Discovery
listening resumes when a join session returns.

`MultiplayerGameScreen` now requires a stack reference for every caller. Its
blocking settings/game fallback and the unused `YOGClientBringup` polling helper
are removed. Browser networking remains WebSocket-based; this cleanup does not
add UDP discovery or raw TCP capability to browsers.
