# Browser files

Saves, replays, custom maps and campaign progress live in the browser profile's
storage for this site's origin. Clearing site data removes them. Changing the
hostname, port or protocol uses a different store. Keep exported backups outside
the browser; these files are not cloud saves.

## Import and export

The load-game chooser has an **Import** button below its file list. Select games
or replays before choosing a file. The custom-game and editor map choosers have
Import and Export controls below their lists. Export downloads the selected file
in its existing Glob2 format. The page has no permanent wrapper controls.

Imports accept `.game`, `.map` or `.replay` according to the visible list, up to
64 MiB. The browser adapter validates names, types and sizes before transferring
bytes. Shared C++ validation then reads the complete game/map state, saved local
UI fields and, for replays, the complete command stream through its terminator.
It rejects unsupported versions, incomplete fields and trailing data. Replay
import is stricter than playback's partial-corruption recovery.

Validation is a cooperative job: Cancel releases partial state and restores the
simulation RNG. It uses a temporary GameGUI with preference persistence disabled;
validating a file must not save preferences. These jobs run from menu choosers,
without an active simulation. Native gameplay uses the same parsers. Reusing the complete loader avoids a
second implementation of the save format, but allocates temporary simulation
and UI objects. Extracting the saved UI-state codec could reduce that cost and
remove the dependency on GameGUI; this interface is open to that change.

An import creates a new file. If its name is already present, it receives a
numbered suffix, for example `Original_(1).game`; existing files are never
replaced. The importer writes the bytes atomically, then waits for durable
browser persistence before showing **File imported** and selecting the new copy.
While persistence is pending, the chooser does not allow navigation away.

On persistence failure, the chooser explains that the file was not saved. Select
Import again to retry, or Export file to download its bytes. Leaving the chooser
abandons and removes the unsuccessful local copy. Validation failures never
write the selected bytes. An unsuccessful import cannot overwrite an existing
save. A failed initial storage restore also prevents importing into that store.

Durable persistence for every legacy writer and wider malformed-file fuzzing
remain release work. The current parser checks
and regression fixtures are not a claim that every possible malformed legacy
file has been qualified.

## Tests

`browser/tests/import.spec.js` drives real file choosers, game controls and
browser database failures. It checks byte digests, duplicate-name preservation,
corrupt/truncated input rejection, loading imported games/maps/replays, and
quota failure followed by export and retry. Read-only diagnostics expose import
state and local file digests; tests do not directly write the virtual filesystem.

`SavegameSafetyHarness` exercises the same import service with injected
persistence completion, cancellation at a cooperative checkpoint, invalid names,
malformed player records, truncated files, duplicate names and abandoned writes.
It verifies RNG restoration and preservation of previous files.

## Campaign progress

The campaign and tutorial menus provide Import progress and Export progress.
Imports merge into the current campaign: completed missions remain completed,
and unlocked missions remain unlocked. The backup's player name is restored.
Mission names, map paths and prerequisite lists must match the current campaign;
the import cannot change those definitions. Starting a new campaign still has
its existing reset behavior; use the loaded campaign to merge with its progress.

On-disk campaign saves retain the existing `.txt` format. Their writes are now
atomic. The menu waits for browser persistence before completing a save or
leaving after edits. On failure it offers Retry save, Export progress and Leave
without saving. Discard restores the previous file's exact bytes, or removes the
new file if none existed, so a later unrelated storage flush cannot commit the
abandoned change. Normal application shutdown retains best-effort saving for
active missions; abrupt browser termination cannot wait for pending I/O.

Progress backups use `.campaign`, a bounded, versioned data-only format. This
avoids importing a full campaign definition containing new map paths. Native
and browser builds share the codec and continue reading legacy campaign saves.
The format is intentionally small and open to revision during review:

| Field | Encoding |
|---|---|
| Signature / version | `G2CP`, then big-endian uint32 `1` |
| Campaign / player name | Length-prefixed byte strings using the game's UTF-8 convention |
| Mission count | Big-endian uint32, at most 1024 |
| Each mission | Name, map path, prerequisite count and prerequisite strings, then unlocked and completed uint8 flags |

String lengths are big-endian uint32. Player names are limited to 512 bytes and
exclude control characters; flags must be 0 or 1. Imports are limited to 1 MiB,
require the complete format without trailing bytes, and reject mismatched
versions or definitions before modifying progress. The file-picker basename
never becomes a save destination. The format is separate from simulation saves
and does not carry accounts or credentials.

`browser/tests/campaign-progress.spec.js` covers round trips, merging an older
backup, malformed files, persistence failure, retry and discard, including an
absent previous file. Recovery is verified from another browser page and after
reload. The native safety harness also checks every truncated backup, version
and definition mismatches, legacy text round trips and injected write failures.

## Editor saves

Map-editor saves use the same checked atomic file replacement as game saves.
Serialization, flush, close or replacement failure leaves the previous file
intact and does not publish a new editor map name. After a local write, the save
dialog stays open until the storage service confirms durable persistence. This
also applies to Save before quit: the editor retains its pending quit decision
and modified state until persistence succeeds.

Quota and transaction failures retain the dialog with retry and file export.
Cancel returns to the editor with unsaved changes still marked; it does not
promise to roll back an already written local file. As with game-save retries,
the local replacement may be persisted by a later successful storage sync.
Export provides a backup independent of that browser store. Failed initial
storage restoration prevents the editor from overwriting stored maps.

The implementation reuses LoadSaveScreen's owned persistence operation and the
shared FileManager writer. It does not introduce browser APIs into the editor.

## Preferences and keyboard bindings

Settings now checks atomic replacement of `preferences.txt` and both keyboard
layout files, then waits for the shared storage service before closing. Native
and browser use the same screen transition. A browser transaction failure or
quota exhaustion retains Settings with Retry and Continue and a visible failure
message. Continue retains the live changes without claiming a durable save;
it does not roll back files already written to the local filesystem, and later
background persistence may save them. While a flush is pending, Settings ignores
completion/cancellation actions.

Each local file replacement is atomic; the three local files are not a single
filesystem transaction. The durable IndexedDB flush uses the existing storage
transaction. Orderly application Quit now performs a checked final preferences write and
waits for browser durability after the gameplay screens have been destroyed.
This also flushes their successful destructor writes. Abrupt tab/process closure
cannot perform that handshake. Diagnostics expose only graphics
flags from preferences, never saved account fields.

## Orderly Quit and campaign authoring

Quit keeps a shared application screen alive while the final preferences write
and storage flush complete. Repeated close requests and Escape cannot skip that
pending operation. A failed write offers Retry save and Quit without saving;
only that explicit latter choice leaves after failure. The final screen says
Game closed and does not leave a frozen Saving message. This is shared with the
native application host; browser persistence requires no gameplay JavaScript.

Closing/reloading the browser tab itself is abrupt and cannot be made to wait
for asynchronous storage. Use the game's completed save operations before doing
so. The shutdown flush cannot repair an earlier failed local campaign/replay
write; it confirms the files successfully present in the local filesystem.

The campaign authoring editor now also waits after its existing atomic campaign
write. Pending saves hide editing/navigation controls. On failure the editor
remains open with an error and OK to retry. Cancel returns to the editor menu;
as with settings, it does not roll back a local replacement already made, and a
later successful flush may persist it. Campaign-definition backup/import/export
is still separate release work from the implemented campaign-progress backups.

`shutdown-storage.spec.js` covers delayed completion, resize, Escape, quota
failure, retry and explicit exit after failure. `campaign-editor-storage.spec.js`
covers delayed completion, quota failure, retry and durable bytes after reload.
The native session harness checks orderly application Quit through the same
application API, including repeated close events.
