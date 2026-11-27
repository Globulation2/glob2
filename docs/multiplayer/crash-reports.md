# Crash reports

Crash reports are [problem reports](problem-reports.md) that a crash sends instead
of a player: the same bundle and endpoint, with one more entry holding the crash.
glob2-online symbolicates each one against the symbols its release uploaded and
files it under a crash group, so maintainers see "this crash, 40 installs, since
0.11.7" rather than 40 separate reports. Nothing goes to a third-party service.

Every platform captures crashes: Crashpad on Android and Linux, MetricKit on
iOS and macOS, Windows' own minidump writer, and the browser's error hooks. The
game also sends a few non-fatal errors for players who chose Always send.

## Capture in the game

`src/app/CrashCapture.h` holds what every platform shares. Each build links one
platform layer: `platforms/mobile/android/CrashCapture.cpp`,
`platforms/apple/CrashCapture.mm` (iOS and macOS) or
`src/app/CrashCapturePlatform.cpp` (Windows, Linux, the browser). Builds without reports
(`problem_reports=0`: F-Droid, the Fire tablet and China editions, distribution
packages) capture nothing.

| Platform | Capture | Left in `reports/crashes/` |
| --- | --- | --- |
| Android | Crashpad's handler, started only when the game crashes | `<id>.dmp`, moved out of Crashpad's database (`reports/crashpad/`) at the next launch |
| Android | `Glob2Activity.captureJavaCrashes`, the default uncaught-exception handler | `<id>.java.txt`: the thread and the stack |
| iOS, macOS | A MetricKit subscriber: the system delivers each crash to the next run, some time after launch | `<id>.metrickit.json`, one `MXCrashDiagnostic` |
| Windows | An unhandled-exception filter (and `SIGABRT`) wakes a thread made at startup, which writes the crashed thread's minidump with `dbghelp`'s `MiniDumpWriteDump` | `<id>.dmp` |
| Linux | Crashpad's handler, shipped as `lib/glob2/crashpad_handler` (or beside the binary) and started only when the game crashes | `<id>.dmp`, moved out of `reports/crashpad/` at the next launch |
| Browser | `shell.html` (`glob2CrashCapture`): a WebAssembly trap, an Emscripten abort, or an error or rejection whose stack is the game's | `<id>.web.json`, a `WebErrorReport` with the `.wasm` file's build ID, in the saved files |

- **Crashpad on Android.** Crashpad comes from the pinned vcpkg baseline
  (Apache-2.0) through the `crash-capture` feature of
  `platforms/mobile/vcpkg.json`; F-Droid's bundle, which F-Droid builds from
  source, leaves it out (`dependencies.py --no-crash-capture`, dependency
  identity `crash_capture: false`). `android.py` packages the handler as
  `lib/<abi>/libcrashpad_handler.so` (about 1 MB per ABI), and Gradle then
  extracts native libraries at install (`useLegacyPackaging`), because Android
  runs executables only from the extracted library directory. The game links
  Crashpad's client (`GLOB2_CRASHPAD`) and starts the handler when it crashes
  (`StartHandlerAtCrash`), with no upload URL: the game sends reports itself.
- **Windows and macOS use the system.** Windows writes minidumps with `dbghelp`
  (part of Windows) from a thread of its own, so a crash that used up the stack
  still gets one; MinGW builds need no Crashpad. macOS uses MetricKit as iOS
  does (macOS 12 and later; the game needs 15), which also works inside the Mac
  App Store sandbox, where Crashpad's handler could not attach.
- **Crashpad on Linux.** `buildsys/crashpad_dependencies.py` builds it from the
  same pinned port through the overlay in `buildsys/vcpkg-ports/crashpad`, which
  uses Crashpad's socket HTTP transport without TLS, so it links neither libcurl
  nor a second OpenSSL (the game never uploads through Crashpad). Builds set
  `GLOB2_CRASHPAD_PREFIX` to its prefix: the GitHub release and GOG Linux
  builds do, and CI's Linux variants job crashes such a build on purpose
  (`test/crash_capture_smoke.py`). Without it, and in distribution packages
  (`problem_reports=0`), Linux captures nothing.
- **The browser** keeps the error in the saved files (IndexedDB) and offers it
  at the next visit like any other crash. Its build ID is the first 32 hex
  digits of the SHA-256 of the `.wasm` file that ran (`Module.glob2WasmFile`),
  which the release workflow's symbol map upload uses too. Errors that are not
  the game's (extensions, a blocked request) are ignored.
- **The crashed session's log.** While the game runs, its recent log and the
  running match (the map seed, then each drawn tick) are mirrored into
  `reports/session.log`, a 64 KB file mapped into memory, so a crash loses
  nothing written before it. The next launch keeps it as
  `reports/previous-session.log`, and the newest crash captured since that
  session began takes it as its `log.txt` and `match`, once. Only the UI thread
  writes the match; nothing touches the simulation thread.
- **Caps.** At most five crashes and 10 MB wait on disk, the oldest dropped
  first; queued reports then follow the
  [outbox](problem-reports.md) caps.

### Non-fatal errors

Some failures do not crash the game but deserve a look: a map or saved game that
fails to load (`GameLoadScreen`), and an online match that fails
(`OnlineMatch::fail`, the reason only). `CrashCapture::reportError` sends them as
kind `error` with an `error.json` entry (`ErrorReport`: category `map-load` or
`network`, and the message), the context and the recent log, only for players
who chose **Always send**, at most five a day per install
(`reports/errors-today`) and once per message per run. The crash worker groups
them by category and message with numbers and quoted names blanked.

### Consent

Crash reports are opt-in. Once the main menu is up, waiting crashes are handled
by `CrashCapture::decide`:

- A player who may send ([problem report](problem-reports.md) rules: aged 13 or
  over, a build and page that allow sending) is asked "Globulation 2 closed
  unexpectedly. Send a crash report?" with **Send**, **Always send** and
  **Don't send**. **Always send** sets `alwaysSendCrashReports`, which
  **Settings > Privacy** turns off again; **Don't send** deletes them.
- Players under 13, or who never said their age, are never asked. Their crashes
  stay on the device, and **Report a problem > Save to file** can add the newest
  one ("The last crash") for them to share.
- **Settings > Privacy > Test crash report** closes the game with a deliberate
  crash in `CrashCapture::crashForTest`, to check the whole path on a release
  build (TestFlight, Play internal testing).

## Crash bundles

A crash bundle is a [problem report bundle](problem-reports.md#the-bundle) whose
`context.json` has a crash kind, plus that kind's entry
(`CRASH_PAYLOAD_ENTRIES` in `services/online/packages/protocol/src/crashReports.ts`):

| Kind | Entry | Content |
| --- | --- | --- |
| `crash` | `minidump.dmp` | A Crashpad minidump |
| `metrickit` | `metrickit.json` | `MXCrashDiagnostic.jsonRepresentation()`, unchanged |
| `web` | `web-error.json` | A `WebErrorReport`: message, stack and the wasm build ID |
| `java` | `java-exception.txt` | An uncaught Java exception on Android, as `printStackTrace` writes it |
| `error` | `error.json` | A non-fatal `ErrorReport`: its category and message |

The context may also carry `match` (the tick and map seed of a running match),
and the bundle the crashed session's log as `log.txt`.
`POST /api/v1/crash-reports` refuses a crash bundle without its entry (400) and
one from a version that has no uploaded symbols (403, so the client drops it);
Report a Problem bundles are accepted from any build. An accepted crash report is
stored as `pending` and queues a `crash:process` job for the crash worker in the
same transaction.

## Symbols

Every shipped build uploads its debug symbols before it is published. Build jobs
hold no secrets, so each hands its symbol files over as an artifact named
`crash-symbols-<workflow>-…`, and `.github/workflows/crash-symbols.yml`, the only
job holding the upload token (the `crash-symbols` environment), runs
`tools/symbols/upload_symbols.py --scan` on them. Each workflow's publishing jobs
need that job, so a release whose crashes could not be read is never published.
The Windows Store build packages and uploads in one job that already holds the
`windows-store` environment, so it uploads with the `.github/actions/crash-symbols`
action itself. Builds of tags cut before crash symbols hand over nothing and skip
the upload.

| Build | Symbols uploaded |
| --- | --- |
| Linux and Windows releases (`release=1`) | `src/glob2.debug` or `src/glob2.exe.debug` |
| macOS bundles | the `.dSYM` that `buildsys/bundle.py` writes |
| Android | each ABI's unstripped `lib/libmain.so`, before Gradle strips it |
| iOS | the archive's `.dSYM` |
| Browser | each `.wasm` build's Emscripten symbol map (`index.js.symbols`), under its build ID |

The GitHub releases (Linux, Windows, macOS), Google Play, TestFlight, the browser
release, Steam, Epic, GOG, the Mac App Store and the Microsoft Store upload
symbols. F-Droid and Fire tablet builds compile report uploads out, so they
upload none. The web client that `deploy-online.yml` builds from `master` has no
release tag yet, and Snap, Flatpak and Fedora packages are built in their own
sandboxes; none of these uploads symbols, so their crash reports would be
refused until they do.
Native release builds outside macOS compile with `-g1` (line tables, no local
variables). The client links with a build ID, is copied to `glob2.debug` (or
`glob2.exe.debug`) and then stripped, so the shipped binary is the stripped
program it was before (`GLOB2_SPLIT_SYMBOLS` in `cmake/compile-flags.cmake`).
MinGW builds name no PDB, so minidumps name their module by its code file
(`glob2.exe`); the upload script fills that name into the `.sym` file's empty
`MODULE` line, which is where `minidump-stackwalk` looks for it.

The script turns native files into Breakpad `.sym` files with Mozilla's
`dump_syms` (pinned with checksums in `tools/symbols/dump_syms.json`; MIT or
Apache-2.0), one per slice of a universal Mach-O file, and refuses a file without
`FUNC` records (a stripped binary). For the browser build it sends the symbol map
under the first 32 hex digits of the `.wasm` file's SHA-256, which the browser
client reports as `buildId`.

`POST /api/v1/crash-symbols?kind=breakpad&version=0.11.8.1` (or
`kind=wasm&version=…&buildId=…`) takes one gzipped file, at most 160 MB:

- It needs `Authorization: Bearer` with `CRASH_SYMBOL_UPLOAD_TOKEN`, a GitHub
  Actions secret set on platform-api; without it configured the route answers 503.
- Only release versions (three or four numbers) are accepted, and the whole file
  must decompress (gzip's checksum).
- A Breakpad file is stored under the module and debug ID of its `MODULE` line,
  in the blob store under `crash-symbols/` with a `crash_symbols` row. Uploading
  the same key again replaces the file.
- The first upload for a version registers it in `crash_releases`, which is what
  lets that version's crash reports in.

Symbol files are kept indefinitely: every release still installable needs them.
The retention job deletes only uploads that no row names.

## The crash worker

`apps/crash-worker` (`services/online/apps/crash-worker/`) runs the
`crash:process` jobs one at a time:

- **Minidumps** go through `minidump-stackwalk` from rust-minidump (MIT), twice:
  once without symbols to learn the modules, then with the uploaded symbols of
  those modules, so the unwind uses their CFI.
- **MetricKit** frames are binary UUIDs and `__TEXT` offsets. The worker maps
  each UUID to a Breakpad debug ID and looks the offsets up in that `.sym` file.
- **Browser** stacks name wasm frames by function index (`wasm-function[N]`),
  which the build's symbol map turns into names.
- **Java** exceptions are already symbolic; frames in `org.globulation2` are
  in-game.

Symbols are decompressed into `CRASH_SYMBOL_CACHE_DIR` on first use, laid out the
way `minidump-stackwalk` searches them, and the least recently used files go once
the cache passes `CRASH_SYMBOL_CACHE_BYTES` (8 GiB). The report keeps the
crashing thread's frames (at most 64, inlined calls first), the crash reason
(`SIGSEGV`, `EXCEPTION_ACCESS_VIOLATION_READ`, `EXC_BAD_ACCESS / SIGSEGV`, or the
browser's message) and its group. A payload that cannot be read marks the report
`failed` at once; other errors are retried three times.

### Grouping

A report's signature is the functions of its top three in-game frames, innermost
first, joined by ` | `. Lines, offsets and compiler clone suffixes (`[clone .cold]`,
`.part.0`) are left out, so a group survives releases and platforms. A frame is
in-game when it is in the game's own module (`glob2`, `glob2.exe`, `libmain.so`,
`glob2.wasm`) and comes neither from a bundled library's sources (`third_party`,
SDL, the C++ runtime, the NDK, MinGW, Emscripten) nor from a runtime function
(`std::`, `SDL_`, `abort`, `malloc`). A stack without in-game frames falls back
to its top named frames, and one without symbols to its top frame's module and
offset. `crash_groups` records each group's first and last release, compared
numerically.

## Admin pages

Administrators see **Admin → Crashes** (`GET /api/v1/admin/crash-groups`): groups
sorted by affected installs (distinct install IDs), then reports, filterable by
version, platform, channel and [triage state](#triage). Each group shows its state
and [GitHub issue](#github-issues), and has the triage controls. **Releases** in the
same tab is the [per-release view](#crash-free-rates). Opening a group lists its reports
(`GET /api/v1/admin/crash-reports?group=…`) with their context and attachments,
and each report's stack (`GET /api/v1/admin/crash-reports/:id`), in-game frames
in bold. **Admin → Problem reports** shows only Report a Problem bundles
(`?kind=problem`).

## Release gates

Crash reports tell what crashed; release gates tell whether a release crashes
more than the one before it, so it is not promoted to more players until that is
understood.

### Session counts

Crash-free rates need the sessions that did not crash. Only installs whose player
chose **Always send** count them, so the rates are over those installs alone:
their sessions, and the crash reports of installs that reported sessions in that
release. The game sends its count with `POST /api/v1/crash-sessions`:

```json
{
  "schema": 1,
  "installId": "3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a",
  "version": "0.11.8.4",
  "versionMinor": 219,
  "distribution": "google_play",
  "platform": "android",
  "day": "2026-11-27",
  "sessions": 3
}
```

- **Body.** `application/json`, a `CrashSessionPing`
  (`services/online/packages/protocol/src/crashReports.ts`), at most 4 KB, no
  other fields. `installId`, `versionMinor`, `distribution` and `platform` are the
  same values as in the install's crash bundles; `version` must be a release
  version (three or four numbers). No cookies or tokens; any origin may send it
  (CORS `*`, preflight allowed for `content-type`).
- **`day` and `sessions`.** A session is one launch of the game, counted when it
  starts, so a session that later crashes is counted too. `day` is the UTC day
  it started on, and `sessions` the running total of sessions the install
  started that day, from 1 to 500. The server keeps the largest total it has
  seen for that install and day, so a resent or retried ping changes nothing and
  a lost answer is safe to retry.
- **In the game** (`CrashCapture::countSession` at launch and
  `CrashCapture::pumpSessions` from the main menu's timer, like the outbox),
  the counts wait in `reports/sessions.json`.
- **When.** Count only while `alwaysSendCrashReports` is on, and only in builds
  that send crash reports (not `problem_reports=0` builds). Send the current
  day's total at most once an hour (for example from the main menu, after a
  launch), and a finished day's final total when next online. Keep at most the
  last seven days on the device; drop a day once its total was accepted and the
  day is over. Turning **Always send** off deletes the waiting counts.
- **Answers.** `204` accepted. `400` a malformed body or a `day` outside the last
  seven UTC days (up to tomorrow, for clock skew): drop that day. `403` a version
  without uploaded symbols (a development or unreleased build): drop the counts
  and do not retry that version. `429` (`Retry-After` seconds) and `5xx`: keep the
  counts and retry later.
- **Limits.** 48 pings per install a day across replicas, 240 per client address
  an hour per replica (memory only) and 60,000 an hour for the instance. The
  address is never stored.

Each install and day is a `crash_session_installs` row (the install's running
total, the only rows holding install IDs, used to count distinct installs), and
each day, release, platform and channel a `crash_session_days` row with its
sessions and distinct installs.

### Crash-free rates

`GET /api/v1/admin/crash-releases` (administrators, **Admin → Crashes →
Releases**) lists registered releases, newest first (versions compared
numerically), ten at a time. For each:

- **Crash-free sessions** (1 − crash reports ÷ sessions) and **crash-free
  installs** (1 − installs with a crash ÷ installs), over opted-in installs only,
  overall and per platform and channel. Only fatal kinds count (`crash`,
  `metrickit`, `web`, `java`); Report a Problem bundles do not.
- Every crash report of the release, whatever its consent, and the distinct
  installs that sent them.
- **New in this release**: the groups whose first release it is, most affected
  installs first, with their triage state.

### Promotion gate

`GET /api/v1/crash-releases/:version/gate` answers whether a release may be
promoted to a wider audience. It takes `Authorization: Bearer` with
`CRASH_SYMBOL_UPLOAD_TOKEN` (503 when unset, 404 for an unregistered version) and
answers a `CrashReleaseGate`: a `verdict` and the `reasons` behind it.

- **Fail** when the release's crash-free sessions are more than 0.5 percentage
  points (`tolerance` 0.005) below the newest earlier release with at least 1000
  opted-in sessions, or when a group new in this release and still `new` is among
  the five groups with the most affected installs in it (`untriagedGroups`).
- **Insufficient data** when the release has fewer than 1000 opted-in sessions
  (`minimumSessions`) and no check failed: never a pass.
- **Pass** otherwise. With no earlier release of 1000 sessions, the comparison is
  skipped and the reasons say so.

`tools/release/crash_gate.py --version 0.11.8.4` calls it (origin
`GLOB2_CRASH_SYMBOL_ORIGIN`, default the official instance) and exits 0 on pass,
1 on fail, 2 on insufficient data (0 with `--allow-insufficient`) and 3 when the
gate could not be asked; `test/build_system/test_crash_gate.py` covers it. The
[release runbook](../releases/downloads.md#crash-reports-before-promotion) runs it
before every promotion.

### Triage

Every group starts `new`. Administrators set it with
`POST /api/v1/admin/crash-groups/:id/state` (`{"state": "triaged"}`, or
`{"state": "resolved", "fixedIn": "0.11.8.5"}` naming the first release with the
fix); each change is written to the admin audit log as `crash_group.state`. When
the crash worker files a report of a `resolved` group from its `fixedIn` release
or a later one, the group is `new` again; `fixedIn` stays, and the admin page
shows it as reopened after that fix. Reports from older releases leave it
resolved.

### GitHub issues

With `CRASH_GITHUB_REPOSITORY` (for example `Globulation2/glob2`) and
`CRASH_GITHUB_TOKEN` (a token that may create issues there) set for the crash
worker, each new group queues one `crash:github-issue` job (job key
`crash-issue-<group>`), which opens an issue and stores its number and link on
the group. The issue holds the signature, the top ten frames with
repository-relative paths, the first release, report and install counts per
platform and channel, and a link to the group's admin page; never install IDs,
logs, descriptions or anything else a player sent. Unset, no jobs are queued.

## Retention

Crash reports follow the problem report retention: bundles (the minidump,
MetricKit payload or browser error, and any log) after 90 days, and rows, with
their symbolicated frames, after two years. A crash group is deleted once its
last report is gone and it was last seen two years ago.
Session counts, with the install IDs they hold, are deleted two years after
their day.

## Deployment

The `crash-worker` service (`deploy/compose.yaml`) runs the platform image with
the worker database role. The image includes `minidump-stackwalk`, built in the
Dockerfile's `crash-tools` stage from rust-minidump's checksum-verified source
release (0.27.0) with its own lockfile, cross-compiled for amd64 and arm64. Its
symbol cache is the `crash-symbols` volume. Set `CRASH_SYMBOL_UPLOAD_TOKEN` in
the host's environment for platform-api, and the same value as a
`CRASH_SYMBOL_UPLOAD_TOKEN` secret of the release mirror's `crash-symbols` and
`windows-store` environments. Until both are set, release workflows fail at
their symbol upload.
Set `CRASH_GITHUB_REPOSITORY` and `CRASH_GITHUB_TOKEN` in the host's environment
for the crash worker to open [GitHub issues](#github-issues); a fine-grained token
limited to that repository's issues is enough.

## Trying it locally

With the API running against a local Postgres
([platform development](platform-development.md)) and
`CRASH_SYMBOL_UPLOAD_TOKEN` set for it:

```sh
CRASH_SYMBOL_UPLOAD_TOKEN=… GLOB2_CRASH_SYMBOL_ORIGIN=http://127.0.0.1:8080 \
  python3 tools/symbols/upload_symbols.py --version 0.11.8.1 build/linux/client/release/src/glob2.debug
MINIDUMP_STACKWALK=/path/to/minidump-stackwalk node services/online/apps/crash-worker/src/main.ts
```

`apps/api/test/crashGroups.test.ts` covers uploads, crash bundles, processing,
grouping and the admin views. `apps/api/test/crashReleases.test.ts` covers session
counts, the release view, the gate, triage and GitHub issues (with a stubbed
`fetch`). Its minidump case runs where `minidump-stackwalk`
is installed (`MINIDUMP_STACKWALK`), against the rust-minidump test dump in
`apps/crash-worker/test/fixtures/`.
