# Client content installation

Downloading, installing and caching shared building, map, music, terrain and generator content.

## Building-family installation

**Building families** on local new-game and editor new-map screens opens
`src/online/screens/BuildingLibraryScreen.cpp`. It browses the selected instance's
public library, installs compatible validated releases and lets the player choose
which families to add to stock buildings. **Family link or ID** also opens an
unlisted family directly; page links must use the selected instance's origin.
Private families require that owner's sign-in through Online. Online browsing needs that instance;
already installed families remain available offline. Selection is stored in the
local profile and applies to subsequent new maps in both flows.

`src/building/BuildingLibrary.cpp` writes pinned releases and the selection index
under `online/buildings/` through `OnlineStorage`. Installation verifies the exact
package, artwork, stock catalog, simulation version and resolved catalog hashes
before publishing the new index. Updates preserve the previous release on failure;
damaged cache entries are reported rather than silently substituted.

Loaded maps, saves and replays use their embedded catalog and frames. Local
selection does not alter those files or server-side room generation. To use a
family online, generate and share a map through the map library, then choose that
map in the room. See [building catalogs](../features/building-authoring.md#online-library-and-installed-families)
for the website editor, package format and limits.


## Map cache

`MapCache` stores platform maps by the SHA-256 of their decompressed bytes as
`online/maps/<hash>.map.gz` (saved games: `<hash>.game.gz`), readable like any gzip map through
`FileManager::openInflatingInputStreamBackend`. `fetch(origin, hash, headers)`
returns a polled download of `<origin>/api/v1/blobs/maps/<hash>`;
`MapCache::blobPath` owns the endpoint path. The download
accepting gzip or raw bytes, refusing anything whose hash differs, and storing
it. Maps are limited to 64 MiB decompressed; the cache keeps at most 256 MiB,
evicting the least recently used. An index (`online/maps/index.json`) keeps
sizes and use order across restarts; unindexed files are removed at startup.
LAN guests store the maps they download from a host in the same cache.


## Music library and offline imports

Settings → Audio opens `MusicLibraryScreen` through the cooperative screen stack.
`MusicSetScreen` owns the synchronized preview and `MusicImportScreen` uses the
host file picker for a ZIP or three labelled Opus files. Browse uses the selected
instance and its existing credentials; Installed and Import remain available in
online-disabled editions. Creation and arbitrary-format conversion live on the
website. Preview temporarily suspends background music; closing the screen
restores it, and focus loss pauses preview.

Online downloads check the API's SHA-256 before entering the same `Music::ImportJob`
used by offline imports. Validation advances in short UI-frame slices and fully
decodes each track. The installer stages all sets, checks metadata, identities,
lengths and collisions, then renames complete directories under the writable
`data/zik/community-<UUID>`. Identical installs are deduplicated; a conflicting
release or bundled soundtrack is never overwritten. Existing untagged soundtrack
directories keep their filename-derived labels; imported sets display the Calm
file's embedded title and artwork.

ZIPs may contain up to ten sets within 64 MiB, with at most 16 MiB per track.
Only ordinary stored/deflated `a1.opus`, `a2.opus`, `a3.opus` entries in a set
directory are accepted. Unsafe paths, links, duplicate entries, incomplete sets,
excessive expansion and checksum failures are rejected. Browser installs are not
reported complete until the host persistence request succeeds. A failed flush
retains recovery bytes and offers retry/export. Local removal cannot delete
bundled sets. Changes affect local music and presentation only; they do not alter
simulation, saved games, replays or the match protocol.


## Custom terrain and resource sets

The map editor's **Set Library** uses the configured instance and signed-in account.
It downloads an exact release with a bounded response and verifies its hash before
import. The dialog previews its terrain/resources, allows selecting individual
entries, and shows license and creator credit. Disk import accepts the same JSON
package offline. The map owns all custom images and definitions after import;
built-in graphics are referenced from installed game data.

The same dialog exposes copied map content and attribution, local edits and an
explicit replacement action for a newer release. Updates are never automatic.
See [resource catalogs](../features/terrain-resource-sets.md#themed-terrain-and-resource-sets)
for package bounds, compatibility and update behavior. Maps and replays do not
contact the set library during play.


## Shared JavaScript generators

The Generators library pins immutable releases for local installation and custom
rooms. A scripted room sends `ScriptGeneratorDescriptor` in its selection and match
setup; every player downloads the resulting ordinary map through `MapCache`.
Joining does not require installing code. Host settings changes clear readiness;
rerolls request a fresh preview, and older results cannot replace the latest choice.
Unavailable workers and rejected settings remain visible failures. The server
rechecks access, moderation and exact engine validation before starting. A new
published release never changes an existing selection.

Clients advertise `generatorSharing: true` in `session.hello.client`. Older clients
receive an update-required response before unsupported room contracts. Ranked
matchmaking retains native generation. See the
[generator publishing guide](../map-generators/javascript.md#publish-and-discover-online)
for visibility, technical validation, installation and explicit updates.

[Multiplayer index](README.md) · [Documentation index](../README.md).
