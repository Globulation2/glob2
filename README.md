# Browser presentation realm diagnosis

Repair revision: 56a9012800499b2b9d118d7a8e13334a967caf33; base 4db5658787434696c50fb92d9adcbcc32919b960.

The archive retains independent browser API probes (serial and actual worker WebGL2), headed and feature-flag negative probes, 193 passing Node contracts, and observed lifecycle events from the original failure. Linux pinned WebKit reports no worker context; Chromium/Firefox report one. Original lifecycle preparation succeeds, finish(false) rolls back, then fullscreen exits. This supports preflight availability checking before window teardown. It does not prove transient context-creation recovery.

Both fresh release Wasm builds and 30 focused real-browser cases are pending. This is diagnostic evidence, not a completed qualification. GPU-dependent assertions are retained for supported contexts; unsupported worker GPU coverage must be reported explicitly. Software and unavailable-choice continuity have new dedicated tests.
