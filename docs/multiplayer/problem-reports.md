# Problem reports

Report a Problem lets a player send the recent log, their newest save and replay,
and a short description to the official glob2-online instance, or save the same
bundle to a file. Every report goes to glob2-online and to no third-party service.
[Crash reports](crash-reports.md) use the same bundle and endpoint, and add
symbolication, crash grouping and [release gates](crash-reports.md#release-gates).

## In the game

**Report a problem** sits on the main menu directly above Credits: in the phone
layout's **More** list, and in the desktop utilities grid in the cell before
Credits. `ProblemReportScreen` (`src/ui/screens/`) asks "What happened?" (at most
2,000 bytes), with three boxes ticked by default (recent log, last saved game, last
replay) and a line saying what every report carries.

| Player or build | Send | Save to file |
| --- | --- | --- |
| 13 or older, online | yes | yes |
| Under 13 or no declared age ([age bands](identity.md#age-bands)) | no | yes |
| Built with `problem_reports=0`, or a portal without online sign-in | no | yes |
| Offline | queued until online | yes |

`ProblemReport::sendPolicy` decides this. Send shows the short report ID the server
returns. Save to file uses the browser download or the mobile document picker; on
desktop it writes the bundle to `reports/` in the player's directory and opens the
save dialog for it from `reports/saved/`, which keeps only the newest copy.

A send that gets no answer, a 408, 429 or 5xx goes to `reports/outbox/` (at most
five reports and 10 MB, oldest dropped first). The main menu's timer
(`ProblemReport::pumpOutbox`) sends queued reports one at a time, 15 seconds after
the menu appears and then every five minutes, never for a player who may not send.
Any other 4xx discards the report: retrying cannot help it. Leaving the screen while a send
is under way queues the report too.

## The bundle

A zip of stored entries, written by `ProblemReport::zip` (`src/app/ProblemReport.cpp`):

| Entry | Content |
| --- | --- |
| `context.json` | `CrashReportContext` (`services/online/packages/protocol/src/crashReports.ts`) |
| `log.txt` | the last 200 log lines, each at most 512 bytes |
| `save.game[.gz]` | the newest `games/*.game[.gz]` in the player's directory |
| `last.replay[.gz]` | the newest `replays/*.replay[.gz]` in the player's directory |
| `minidump.dmp`, `metrickit.json` or `java-exception.txt`, and `crash-log.txt` | "The last crash", shown when a [crash](crash-reports.md#capture-in-the-game) waits on the device (players who may not send keep theirs for Save to file) |

Shipped content is never attached. Files that would push the bundle past 10 MB are
left out and named on the screen. The context holds the schema number, the kind
(`problem`), a random install ID (`reports/install-id`, made on first use), a random
submission ID per report, the
version, `VERSION_MINOR`, `SIM_REVISION`, the distribution, the platform and SDL's
operating-system name, the device class (desktop, tablet or phone, from the
resolved presentation) and the `DeviceProfile` tier, the description and the time.
`ProblemReportTest.cpp` checks the client writes exactly the fields of the
`CrashReportContext/problem-report` protocol fixture.

The log tail (`ProblemReport::installLogCapture`, started when the windowed game
launches; headless and tool commands are untouched) passes
`std::cout`, `std::cerr`, `std::clog` and SDL's log through unchanged and keeps
their last lines. Screen changes are added as `Glob2 screen ready: <screen>`
breadcrumbs on every host; mobile hosts also print them. None of this touches the
simulation thread, so checksums and replays are unaffected.

## The endpoint

`POST /api/v1/crash-reports` (`services/online/apps/api/src/crashReports/`) takes the
bundle as `application/zip` without signing in, and answers `201 {"id": "K7M2QX9D"}`
(eight letters and digits without 0, O, 1 or I).

- The bundle is at most 10 MB, packed and unpacked. The zip reader reads only the
  central directory and refuses encryption, zip64, spanned archives, unsafe or
  repeated names, more than 16 entries and overlapping entries.
- `context.json` must match `CrashReportContext` exactly; extra fields are refused.
- A bundle whose `submissionId` was already stored answers `200` with that report's
  ID, so retries (a lost answer, or a report queued after the player left the screen
  mid-send) are stored once.
- Each install may send 20 reports a day (a shared Postgres limit keyed by install
  ID). Each address may send 60 an hour, counted in memory per replica (at most
  10,000 addresses, oldest evicted), so client addresses are never stored. Install
  IDs and forwarded addresses are chosen by the client, so the instance also accepts
  at most 600 reports an hour in all.
- The route answers CORS preflights for any origin. It takes no cookies or tokens,
  and browser builds on game portals post from other origins.
- The bundle is stored in the blob store under `crash-reports/`, with a summary row
  in `crash_reports` (migration `0061`).

Administrators (`role = admin`) see reports under **Admin → Problem reports**
(`GET /api/v1/admin/crash-reports?kind=problem`), newest first, and download the bundle or one
attachment (`…/:id/bundle`, `…/:id/attachments/:name`). Every download is written
to the admin audit log as `crash_report.download`.

The worker deletes bundles after 90 days and summary rows after two years
([retention](../hosting/operations.md#retention)). The privacy policy's
[problem reports](../mobile/privacy-policy.md#problem-reports) section is the
player-facing description; keep it, and the store data declarations, in step with
any change to what a report holds.

## Builds without uploads

`problem_reports=0` ([build options](../development/build-options.md#free-launch-profiles))
compiles sending out and leaves Save to file. It is the default for the China and
Fire tablet editions; the F-Droid build (`android.py --fdroid`) and the Fedora spec
pass it. Snap, Flatpak, the stores and our own downloads send.

## Trying it locally

Run the API against a local Postgres
([platform development](platform-development.md)) and point the game at it:

```sh
GLOB2_PROBLEM_REPORT_ORIGIN=http://127.0.0.1:8080 glob2 play
```

The override accepts any instance origin; plain `http://` only for loopback hosts.
