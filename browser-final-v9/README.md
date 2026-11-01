# Final focused Bulgarian browser evidence

Release source graph94021f77dab35b1d3603d74b7b9648bfdbedff40 was confirmed clean by root's ordinary release both-variant SCons99806 rc0. The exact source, build-input and staged-package hashes bind actual execution; release JavaScript/WASM has no embedded GitSHA claim. Package-static staging and --verify passed.

The four tracked Bulgarian cases passed together sequentially: startup2.1s, serial24.7s, threaded49.2s, English-browser explicit selection and persistence39.0s. Both actual WASM byte streams were buffered from the real upstream server and passed unchanged to the browser; exact frozen hashes, real executionMode and full Bulgarian VFS bytes matched.

Actual startup CDP checks at desktop DPR1/DPR2 and phone confirm all33 status glyphs use custom Glob2 Sans, downloaded core SHA466fb75762954c81cd683538f08c4e36773dc27cb5c11339e3e1a675777a6048, font/ttf MIME, gzip. All44 locale catalogs, both release WASM files, core font and licenses were fetched over actual HTTP and matched frozen hashes. Inherited standalone decorative branding and theme URLs404 remain recorded.

The immutable index has37 fresh actual PNGs covering main/settings, responsive Custom Game, tutorial modal/HUD/help, normal Statistics and phone editor at150%. The new phone toolbar uses two columns and three rows; QA was corrected through actual viewed labels and real touchscreen taps. Touch Escape left the editor unchanged; tapping its visible Menu button opened the real scrolling menu and footer. Original failed QA assumptions and their diagnostics are preserved. No simulation or visibility flags changed.

Fresh JavaScript units:171 total,168pass,3fail. All3 are exact inherited master replay-fixture failures: replay186 is outside187..191; two Studio fixtures remain186 vs191, failing before simulation-version checks. The same unit test, fixtures, Version.h and ReplayReader.h are byte-identical to frozen master0b6a9fe67b; an actual minimal baseline run reproduces all3 failures. This evidence does not claim a green complete JS suite.

Linux Chromium153.0.8010.12, Node24.22.1. Physical devices were unavailable. Some Custom Game captures show its preview generating. Historical v8 screenshots remain historical and were not relabeled. Independent visual approval is separate from this capture worker's personal inspection of the seven paths listed in run-summary.json.

Use publish-selection.json for accessible review evidence; it excludes static packages, runtime binaries, dependencies and traceZIPs. Source/package guard passed after execution. All own browser processes exited and the own server was stopped; port42771 is closed.
