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

Campaign-progress import/export, durable persistence for every legacy writer,
and wider malformed-file fuzzing remain release work. The current parser checks
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
