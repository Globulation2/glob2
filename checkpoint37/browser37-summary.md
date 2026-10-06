# Browser37 focused verification

Tested clean revision `7fe776834664300abf24270ed2ac16822885cf5c`. The serial and threaded browser application/test builds completed successfully (web-build-20.log). Source revision/status and all recorded browser binary/data/identity hashes stayed unchanged through validation; see provenance.json. No retries or timeout relaxations were used. All planned processes have stopped, including the temporary server.

## Outcomes and coverage limits

| Cohort | Result | Interpretation |
| --- | --- | --- |
| HUD, Chromium/Firefox/WebKit × serial/threaded | 6/6 runs pass; exact2 C++ cases per run | Valid coverage of the cold helper and real Scene/display marker bounds, instant-row omission, downstream slider hit testing and checksum invariance. |
| Torus root/default entry (artifact originally named torus-serial) | 7/9 pass,2 Firefox assertion failures | Root loader automatically selects execution mode. This was **not forced-serial coverage**. |
| Torus /threaded/ entry | 9/9 fail before application startup | Artifact runner defect: the URL serves a directory listing, so glob2Diagnostics is undefined. These are **harness errors, not rendering regressions or coverage**. |

Raw Playwright total:13 passed,11 failed,0 skipped,24 cases. This total must not obscure the distinction between2 real render/assertion failures and9 invalid-entry harness failures. The original runner, labels, logs and manifests remain intact; coverage-label-correction.json records the corrections explicitly. Forced-serial torus and valid explicit-threaded torus remain unverified. No replacement cohort was run to clear or relabel failures.

## Retained Firefox assertion failures

In both default-entry WebGL tests, WebGL2/torus-enabled/settled state and fitting-frame checks succeeded, but the existing120-second `skyShare >0.6` assertion returned0. The context-loss case failed its initial sky assertion **before** loss/recovery was exercised. Chromium and WebKit passed switching, recovery and software checks; Firefox software passed.

The retained full screencast visibly shows a fitted torus with surrounding sky. Read-only decoding of the actual200×100 screenshot argument independently confirms sky share0; common sampled colors include RGB(8,19,17), rather than the expected near(6,9,15). This is diagnostic evidence, **not** proof of unavailable WebGL, an environment cause, a master regression, or a passing render gate. No master comparator was run. See firefox-pixel-diagnostics.json and the failure trace/error-context/last-frame/last-sampled-clip files under torus-serial-results.

HUD screenshots were visually inspected: mixed service markers stay within their shared row, and the instant-services panel omits that row while preserving the subsequent production/worker control layout. Raw BMPs, cropped inspection PNGs and exact-case XMLs remain under hud/.

Browser versions: {"chromium": "153.0.8010.12", "firefox": "155.0", "webkit": "26.6"}. Exact commands, toolchain and frozen source/binary provenance are retained in provenance.json; per-case results and classification are in results-summary.json. This evidence establishes focused HUD compatibility, not full browser rendering acceptance.

## Separate corrected-loader verification

After the original24-case closure, a separately authorized startup-only smoke used the correct root query URLs and checked actual modes/fallbacks. Five of six cases passed; all reached MainMenu. WebKit's threaded request fell back to serial with `worker shared memory unavailable`, so its requested-mode assertion failed. Chromium/Firefox threaded and all three forced-serial startups passed. Source/browser hashes stayed unchanged. See loader-smoke/README.md and summary.json. No matches or torus assertions were rerun, and this does not fill missing torus coverage. Corrected future reproduction files are prepared under corrected-reproduction but remain unexecuted.
