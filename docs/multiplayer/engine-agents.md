# Engine agents and map catalog

How versioned engine jobs validate content, verify matches and maintain the shared map catalog.

## Engine agents

An engine agent (`platform/apps/engine-agent`) wraps one glob2 binary and runs
the jobs that need the engine. It runs each job as a separate headless process
with a stripped environment. Because that process loads uploaded maps, saves and
match records with the legacy C++ loader, the agent holds no database or blob-store
credentials at all: it reaches the platform only through `platform-api`'s internal
engine API, with a bearer agent key (`ENGINE_AGENT_KEYS`/`ENGINE_AGENT_KEYS_FILE`
on the API, `ENGINE_AGENT_KEY_FILE` on the agent; shapes in the protocol package's
`jobs.ts`):

| Call | Purpose |
| --- | --- |
| `POST /internal/v1/engine/agents/heartbeat`, `DELETE …/agents/{id}` | Announce the agent's sim version and kinds (`engine_agents`). |
| `POST /internal/v1/engine/jobs/lease` | The oldest queued job of the agent's sim version and kinds, with a lease token, or `204`. |
| `POST …/jobs/{id}/extend`, `…/release` | Keep the lease while the engine runs; give the job back for a retry. |
| `POST …/jobs/{id}/result` | Report the result or failure (idempotent per lease). |
| `GET /internal/v1/engine/blobs/{sha256}` | A blob whose hash the leased job's payload contains, and no other. |
| `PUT /internal/v1/engine/blobs?contentType=&visibility=` | Store an output blob by content (known content types only). |

Calls about a job carry its lease token in `X-Glob2-Lease`.

### Sim version and partitioning

At startup the agent learns its sim version from the binary rather than from
configuration, so a mislabelled image cannot serve the wrong version:

- `VERSION_MINOR` and `NET_PROTOCOL_VERSION` come from `glob2 info catalog --format json`
  (`save_version`, `protocol_version`).
- The data hash comes from the binary when it reports one: a `data_hash` field in
  the catalog, or `glob2 info sim-version --format json`, which prints
  `{"versionMinor","netProtocol","dataHash"}`. Current engines advertise this
  command in the domain catalog. Before reading that catalog, the agent probes
  `glob2 help --format json` with a timeout and requires description schema 1 and
  CLI version 2. Older CLI binaries are rejected before workers lease jobs.
- For a domain catalog that does not report a hash, `ENGINE_DATA_HASH` (or a full
  `ENGINE_SIM_VERSION` key) supplies it. Any value that disagrees with what the binary reports stops the
  agent at startup.

The agent then registers and leases only jobs of its own sim version. A job can
therefore reach only a binary that computes the same games. A job that reaches
the wrong version anyway is a routing bug and fails loudly.

### Job contracts

Payloads and results are the protocol's `engineJobs` schemas. The agent checks
every result against its kind's schema before reporting it. Maps and saves are
stored decompressed, so their key is the SHA-256 of the bytes clients load. Every
blob the agent stores is also registered in `blobs`.

| Kind | Engine command | Result |
| --- | --- | --- |
| `generate-map` | `map study <method> --seed <seed> --candidates <n> --set k=v… --write-map --output-dir` (structured; method ids and revisions from the catalog) | map blob hash, size, dimensions, team count, chosen seed, start quality |
| `validate-map` | `map preview <file> --report-file report.json` (the game's own loader, no simulation) | `valid: true` with the decompressed hash, dimensions, team count and the file's format version, or `valid: false` with a reason |
| `render-preview` | `map preview <file> --output preview.png --preview-size <px>` | PNG blob hash and pixel size |
| `verify-match` | `match verify <record> --map-file <file> --output-dir <dir>` | `verified`/`diverged` with the outcome, team statistics and timelines, or `unverifiable` |
| `validate-buildings` | `assets compose-buildings --format json --package <manifest> --artwork-bundle <bundle>` | Archive and stock hashes, suite version, resolved catalog hash and snapshot, and optional artwork hash; deterministic rejection returns `valid: false` and a reason |


Before running the generator, the agent checks the descriptor against the
catalog. An unknown or editor-only generator, a revision this binary does not
produce, a parameter the generator lacks or a value outside its registered values
is a `bad_request`. So is a non-zero `startingUnitLevel`, which the structured
command cannot express yet.

Validation rejects these files, with a reason:

- files that are not maps or saves, and corrupt gzip;
- files over `ENGINE_MAX_MAP_BYTES` (decompressed, so a gzip bomb stops at the
  limit);
- maps written by a newer engine;
- a map uploaded as a save, or a save uploaded as a map;
- maps with a side over 512 tiles, or with 0 or more than 12 teams;
- anything the loader refuses.

Validation reads the file's format version from the first fields of the map
header, after the engine has loaded the file. This small read in
`engineCli.ts` is the only binary parsing in the platform. It is needed because
the map report gives the engine's version, not the file's.

**`match verify` output.** The engine writes (see
[headless replays](../development/headless-replays.md#verifying-a-match-record)), and
the agent reads:

- `<out>/verdict.json`: `{"verdict":"verified"|"diverged"|"unverifiable","seats"?,"reason"?}`;
- `<out>/result.json`: the `HeadlessRunner` game result (team outcomes,
  elimination ticks, prestige, counters and the 512-tick `history`);
- `<out>/match.replay`.

A `verdict.json` decides the verdict whatever the exit code. Without one, exit 2
is an invalid request and anything else is an engine failure. The agent stores
the replay and result.json as blobs (`replayHash`, `resultHash`). Each team in
the outcome carries the final counters as `statistics` and the history as
`timeline` points `{tick, units, buildings, prestige, hp, attack, defense}`.
All parsing of engine output lives in `apps/engine-agent/src/engineCli.ts`; a
change to the engine's command line or output files changes only that module
(and its fixtures, which were captured from a real binary).

**Failures.** Failures are handled by how likely they are to repeat:

- Input problems (`bad_request`) and contract breaks (`internal`) are reported at
  once.
- Timeouts, crashes and transfer errors give the job back (released with a
  backoff of 5 s, 10 s, 20 s …) for another lease.
- On a job's last attempt, any remaining error is reported as `internal`, so the
  platform is never left waiting for a result.

**Process limits.** Each engine process runs in its own scratch directory, which
is also its `HOME` and `GLOB2_USER_DIR`. It gets a minimal environment, and its
whole process group is killed at the wall-clock timeout
(`ENGINE_TIMEOUT_{GENERATE,INSPECT,VERIFY}_S`). It also runs under `ulimit` CPU
and file-size limits, and on Linux an address-space limit (`ENGINE_MEMORY_MB`).
Output files are read back only up to a size limit.

### Results on the platform

The worker's `platform:engine-job-result` handler completes the `engine_jobs`
row. For `verify-match` it also records the verdict and applies ratings in the
same transaction, and stores team statistics and timelines in `match_team_stats`.
It links the record, replay and result blobs in `match_artifacts`, but only blobs
registered in `blobs`. For a generate-map job, it marks the generated map ready or
failed, whether a room, an on-demand queue start or the warm pool asked for it.
Generation and validation results also carry resource experiment display metadata
and the keys required by resources already present on the map. These are stored
with generated maps, uploads and catalog versions, then copied into rooms and
match setups. Required keys remain enabled when room settings change. The engine
validates this metadata against the embedded resource catalog; the platform never
uses display metadata as a substitute for the map's definitions.

The aggregate views from migration 0004 cover verified, ended matches of the
last 90 days:

| View | Contents |
| --- | --- |
| `match_results_view` | One row per seat of every verified match, with queue, generator and map. |
| `recent_win_rates_view` | Games, wins and win rate per player, by queue, map or generator. A player is an account, or an AI id at one sim version. |
| `recent_game_lengths_view` | Mean, median, p90 and maximum length in ticks, by queue and by generator. |
| `team_timeline_view` | The 512-tick samples of each team as rows. |
| `account_economy_curves_view` | A player's units, buildings and prestige at each tick of each match, next to their own average at that tick. |

### Warm map pool

The worker leader runs `WarmMapPool.refill()` every 10 seconds. A sim version
counts as served when an agent running `generate-map` was seen in the last five
minutes. For each queue, served sim version and map pool entry, the pool keeps
`WARM_MAPS_PER_ENTRY` untaken maps (default 2, 0 turns the pool off), either
ready or still generating, and more while an entry is busy: as many as were taken
in the last 15 minutes, up to `WARM_MAPS_MAX_PER_ENTRY` (default 8). For any
shortfall it requests generated maps with fresh seeds.

The pool is a layer over `generated_maps`, not a second generation cache. Each
warm map is a `generated_maps` row requested ahead of time through the same
`requestGeneration` call, and completed by the same `applyMapJobResult`, as room
maps and on-demand queue starts. `warm_maps` holds only the pool bookkeeping:
the queue, the pool entry, the generated map's descriptor hash, and when and for
which match it was taken. Readiness, the job, the map hash and any failure are
read from `generated_maps`.

- An entry that fails three times in ten minutes waits for the window to pass.
  This happens, for example, when the configured revision is not the binary's.
- Maps still generating after 30 minutes are given up and replaced.
- Maps of entries removed from `instance.yaml` are dropped.
- A pool map dropped before it was taken is deleted with its generated map, so
  its blob is collected. A taken map keeps its generated map, like any map a
  match was played on; taken rows are deleted from `warm_maps` after 24 hours.

`takeWarmMap(db, queueId, simVersionKey, { entry?, matchId? })` (exported by
`@glob2/play`) gives a match starter the oldest ready map, using
`FOR UPDATE SKIP LOCKED`. It returns the descriptor with its seed (MatchSetup
`map.generator`), the map hash (`map.hash`) and the generation result, or
`undefined` if none is ready. The next refill replaces a taken map.

Engine agent freshness (`ENGINE_AGENT_FRESH_SECONDS`, `freshAgentSimVersions` in
`@glob2/core`) is the one definition the pool, the API's served-version list and
the stale engine-job sweep use.

### Scaling and operation

- **More throughput:** run more agents of the same image. They lease from the
  same queue (`FOR UPDATE SKIP LOCKED`), and each runs `ENGINE_CONCURRENCY` jobs at
  once, polling every `ENGINE_POLL_MS` when idle.
  Verification is the costly kind, since it runs whole games, so size
  `ENGINE_TIMEOUT_VERIFY_S` and the replica count for the longest games played.
- **An agent dies mid-job:** its lease (two minutes, renewed every 40 seconds while
  the engine runs) runs out and another agent leases the job; each lease counts
  as an attempt (three by default). When the last attempt's lease runs out, the
  worker's scheduler reports the job failed (`failAbandonedEngineJobs`), so nothing
  waits forever. Results are applied once, keyed by job id.
- **Serving an older sim version** (a verifier image for an old version, so its
  matches can still be verified and its rooms still get maps):
  1. Build the engine at that version's tag, with the same compiler image and
     flags as its release. The image must compute byte-identical games, so
     check it with the replay verification guide
     (`docs/development/headless-replays.md`).
  2. Build the agent image from that binary plus this wrapper.
  3. Set `ENGINE_DATA_HASH` if the old binary cannot report its own hash.
  4. Run the image next to the current one. Once it registers,
     `GET /api/v1/instance` lists the version as served, and its jobs flow to it.
  5. When no agent of a version has been seen for five minutes, clients of that
     version get `update_required`.


## Map catalog

The catalog keeps shared maps and their versions. The REST routes
are in `apps/api/src/maps/` (`routes.ts`; rules and views in `catalog.ts`). The
worker applies engine-job results to versions (`packages/play/src/play/catalog.ts`).
Product defaults that are still provisional live in `CATALOG_RULES`.

**Maps and versions.** A map has an owner, a title, a description, a visibility,
how it was made (`hand` or `generator`, with the generator descriptor if known)
and a moderation flag. Each upload of different bytes is a new version, keyed by the
SHA-256 of the bytes clients load. A version records the sim version that checked
it and, once valid, its dimensions, team count, the format version it was saved
with (`minVersionMinor`, the oldest engine that can load it), the map name stored in
the file and a preview. `maps.latest_version_id` points at the newest valid
version. Listings show and filter on it.

**Upload.**

1. `POST /api/v1/maps` with `CreateMapRequest` creates the map (`201 MapInfo`).
2. `POST /api/v1/maps/{id}/versions?simVersion=<key>&notes=…` takes the
   file, plain or gzip-compressed, as `application/octet-stream`, up to
   `UPLOAD_MAX_BYTES`. The API unpacks gzip and checks the map header first, with
   the same plain-language `400` answers as room uploads
   ([rooms and matches](rooms-and-matches.md)). The upload names its sim version;
   without one (web uploads), the newest version the instance serves is used. Only
   the owner may upload. If the same bytes were already checked as a room upload
   (`/api/v1/uploads`) for that sim version, the version reuses the verdict.

   The web app's upload form checks the file with `POST /api/v1/uploads` first and
   polls it; only a file the game loads gets a map (step 1) and a version, so a
   failed upload leaves no empty map page behind.

3. The bytes become a private blob. The version row is written with two job ids,
   and only then are the jobs submitted, so a fast result always finds its row:
   - `validate-map` (format `map`);
   - `render-preview` (512 px).
4. The worker applies the results: facts and `validation: valid`, or `invalid`
   with a reason, then the preview. A file whose validated hash differs from the
   upload (for example a gzip-compressed upload) is invalid.
5. Bytes already checked for the same sim version, on any map, reuse that
   validation and preview without new jobs. Uploading the same bytes to the same
   map again answers the existing version (`200`).

Each replica allows 20 new maps and 20 version uploads per account per hour, and
a map keeps at most 50 versions. Owners edit title, description and visibility
with `PATCH /api/v1/maps/{id}`. They delete a map with `DELETE /api/v1/maps/{id}`,
or one version with `DELETE /api/v1/maps/{id}/versions/{hash}`. Deleting removes
the catalog rows but keeps the blobs, because matches and rooms may still use the
bytes.

**Visibility.** Moderators and administrators see every map. Every other caller
gets `404` for a map they may not see, so its existence does not leak.

| Visibility | Listed | Map, versions, file, preview, blob by hash |
| --- | --- | --- |
| `public` | in `GET /api/v1/maps` and the owner's public list | anyone, signed in or not |
| `unlisted` (default) | no | anyone with the id or hash |
| `private` | no | the owner |
| hidden by a moderator | no | the owner (with `hiddenReason`) |

Pending and invalid versions are shown only to the owner. Guests may create
unlisted and private maps but not publish them. `GET /api/v1/blobs/maps/{hash}`
applies the same rules to catalog versions, alongside its upload, room and match
rules. Rooms may choose `{kind: "catalog", hash, mapId?}` when the version is
valid, its map is not hidden, the map is public, unlisted or the host's own, and
`minVersionMinor` is no newer than the room's engine.

**Browsing.** `GET /api/v1/maps` lists public maps that have a valid version. It
takes these filters:

- `owner=me` lists every map the caller owns; `owner=<accountId>` lists that
  account's public maps, or all of them for the owner and moderators;
- `teams` (exact), `minSide` and `maxSide` (the larger side, in tiles), `madeWith`
  and `q` (title search) filter on the latest version;
- `sort` is `recent` (default), `likes`, `plays` or `downloads`, newest or
  highest first;
- `limit` (default 30, at most 100) and `cursor` page through the results.

`GET /api/v1/maps/{id}` returns `MapDetail`: the map, its versions (newest first)
and what the caller may do (`viewer.owner`, `moderator`, `liked`, `reported`).
`GET /api/v1/maps/{id}/versions/{hash}` returns one version. `…/file` serves the
bytes as an attachment, and `…/preview.webp` serves the preview.

**Stats.**

- **Plays:** ended matches on any version of the map, counted when the relay's end
  report is applied (once per match).
- **Downloads:** `…/file` requests, counted once per downloader and day (by account,
  or by address when signed out). The owner's own downloads do not count.
- **Likes:** `PUT` and `DELETE /api/v1/maps/{id}/like` count registered accounts once
  each, and answer `MapLikeResult`.

**Reports and moderation.** Any signed-in account may report a map it can see with
`POST /api/v1/maps/{id}/reports` (`MapReportRequest`). Each account may have one
open report per map; a repeat answers the open one. Each account may file 10 reports
per hour. Moderators and administrators can then use these routes:

| Route | Effect |
| --- | --- |
| `GET /api/v1/admin/map-reports?status=open\|resolved\|dismissed\|all&mapId=&cursor=` | Reports, newest first, with the map and reporter |
| `POST /api/v1/admin/map-reports/{id}/resolve` | `ResolveMapReportRequest`: `resolved` or `dismissed`, an optional note, and optionally hide the map |
| `POST /api/v1/admin/maps/{id}/hide` | Hide with a reason (`MapHideRequest`) |
| `POST /api/v1/admin/maps/{id}/unhide` | Show again |

Administrators may also delete any map. Every moderation action is written to
`admin_audit_log` with `target_type` `map`.

[Multiplayer index](README.md) · [Documentation index](../README.md).
