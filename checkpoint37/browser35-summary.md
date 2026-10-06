# Browser35 focused verification

Clean revision `63a3b14098323ef0b5f8657c1d78521f29cb7ade`, source tree `ae346cc8deec96289ca84b53a46f6172c0d2320376985cf53ae7a994e6a8bfcd`. Pinned Emscripten4.0.15 web18 build at-j4 passed.

**21/21** explicitly selected Playwright cases passed, with zero failures, skips or flaky cases: Chromium, Firefox and WebKit each run four replay modes (serial; threaded1/2/4), committed-match verification, and serial/threaded custom-building compositions.

All **15/15** replay/match traces match native35 exactly; native35 committed-match verdict is verified. All **six** composition runs passed exactly73 cases, with no inner failures/errors/skips. Counts and exact trace hashes are retained in `comparison.json`; command, source and binary provenance in `provenance.json`; build log is `../web-build-18.log`.

An additional **24/24** browser cases passed against the actual registered harness: six RuntimeContinuation runs (five cases each), six GradientPreparation runs (12 cases each), six CortexActionCoverage runs (14 cases each), and six MapQuery runs (42 cases each). Each suite ran serial and threaded in Chromium, Firefox and WebKit, for438 inner executions. The new tracker, natural-goal cache, observation-qualification and wrapped-distance cases are included. Commands, inner case names, and provenance are retained in `runtime-comparison.json`.

This is **not** a full33-case run. Unchanged scripting and Markets browser coverage remains explicitly tied to checkpoint27 (`../browser27/`), whose full33 suite and six strict73-artifact scripting comparisons passed. No scripting/Markets results were copied into this checkpoint corpus.

Native and browser executable hashes remained unchanged, and source remained clean through final checks. All owned processes have finished; no timings were run. Native Windows/macOS and mobile verification remain outside this lane.
