# Crash reports

Crash reports are [problem reports](problem-reports.md) that a crash sends instead
of a player: the same bundle and endpoint, with one more entry holding the crash.
glob2-online symbolicates each one against the symbols its release uploaded and
files it under a crash group, so maintainers see "this crash, 40 installs, since
0.11.7" rather than 40 separate reports. Nothing goes to a third-party service.

Android (Crashpad, plus uncaught Java exceptions) and iOS (MetricKit) capture
crashes; Crashpad on Windows, macOS and Linux and the browser's error hooks come
later and only need to send the bundles described here.

## Capture in the game

`src/app/CrashCapture.h` holds what every platform shares; the platform layers
are `platforms/mobile/android/CrashCapture.cpp` and
`platforms/mobile/ios/CrashCapture.mm`. Builds without reports
(`problem_reports=0`: F-Droid, the Fire tablet and China editions, distribution
packages) capture nothing.

| Platform | Capture | Left in `reports/crashes/` |
| --- | --- | --- |
| Android | Crashpad's handler, started only when the game crashes | `<id>.dmp`, moved out of Crashpad's database (`reports/crashpad/`) at the next launch |
| Android | `Glob2Activity.captureJavaCrashes`, the default uncaught-exception handler | `<id>.java.txt`: the thread and the stack |
| iOS | A MetricKit subscriber: iOS delivers each crash to the next run, some time after launch | `<id>.metrickit.json`, one `MXCrashDiagnostic` |

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
version, platform and channel. Opening a group lists its reports
(`GET /api/v1/admin/crash-reports?group=…`) with their context and attachments,
and each report's stack (`GET /api/v1/admin/crash-reports/:id`), in-game frames
in bold. **Admin → Problem reports** shows only Report a Problem bundles
(`?kind=problem`).

## Retention

Crash reports follow the problem report retention: bundles (the minidump,
MetricKit payload or browser error, and any log) after 90 days, and rows, with
their symbolicated frames, after two years. A crash group is deleted once its
last report is gone and it was last seen two years ago.

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
grouping and the admin views. Its minidump case runs where `minidump-stackwalk`
is installed (`MINIDUMP_STACKWALK`), against the rust-minidump test dump in
`apps/crash-worker/test/fixtures/`.
