# PR 974 verification evidence

Final tested feature commit: 8b33938c25c1c42df52a8b1e534309b4367e01d7.
Integrated base: 93273e6361b7a27a8ed251e9182a9099f24ab4dc.

Final native checks: 201 cases, 199 passed, 0 failed, 2 display-only skips.
The source ownership contract and simulation-version contracts pass. Native
1,500-tick and committed-match traces are retained with manifests; the former
matches all previously exported native/browser traces.

Earlier comprehensive validation at 65f1e6a7659435db733505a9d22edb2d29a7ff67:
2,268 native cases (2,072 passed, 195 display skips, one independently reproduced
SDL3_image 3.4.8 normalization failure), and 54 passing browser checks across
Chromium, Firefox and WebKit, serial and threaded runtimes. These reports retain
their original provenance and are not claimed as tests of the final integration.

Final integration covers format-150 area-state migration, RNG and active area
continuation, map arithmetic, resource growth, AI, timing, Generator Studio,
compatibility and all golden cases. The RNG primitive and salt domains are
unchanged since cross-runtime validation. Browser execution of the final combined
master integration was not repeated. Native Linux/Windows and manual gameplay
were unavailable. Review of changed gameplay trajectories remains useful.

`verification.json` records producer fingerprints, environment, compiler flags,
dependencies, exact commands, trace hashes, results and limitations.
`review-evidence.tar.gz` contains reports, logs, checksum traces, representative
saves/replays, frozen generator packages and the SDL baseline reproduction.
Generated evidence is kept on this separate branch, outside the feature diff.

Archive SHA-256: f4bd224bf6f9bd7c5fb1fe3888d54af76337343a29da75ed72ebaf0b25e70e75
