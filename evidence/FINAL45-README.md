# Configurable units: final validation evidence

The feature revision is `5efe8b6f0326b59b7ace524992612ef07e861581`. Simulation execution was
produced at `9e6f406b634023702eb34125916f74fe453a5e54`. The final source bridge records
exactly 17 reviewed non-simulation paths; final scoped execution covers that delta.
Original producer identities, failures, conditional/display skips and platform
limits are retained. This is a foundation for developer-authored definitions;
only worker, explorer and warrior ship. No final performance claim is made.

`final45-validation-summary.json` indexes source-bound receipts and describes the
limits. The execution volume includes actual commands, build/runtime provenance,
inventories, JUnit, traces, selected hosted artifacts, reviews and original failure
records. The sampled complete-state volume contains every explicitly selected raw
state/input/save/proof file and the continuous checksum, order and event traces.
Its selection manifest lists omitted expanded diagnostic sidecars by path, size
and hash. It contains no dependency on an earlier archive. The streaming integrity
receipt verifies all 1,982 selected files, including lossless internal hard links.

## Restore

Download each `*-parts.json` manifest and its listed part basenames from this same
immutable evidence revision. Concatenate the parts in manifest order into the
named archive. Check each part's byte count and SHA-256, then the concatenated
archive's SHA-256 before extracting the tar.gz. The manifest paths describe their
original repository locations; Git stores each downloadable part directly under
`evidence/`. Extracted paths begin with `artifacts/unit-foundation/`.

- `final45-execution-evidence.tar.gz-parts.json`: closed native, sanitizer, master
  integration, platform and final scoped evidence.
- `final45-sampled-complete-state-evidence.tar.gz-parts.json`: selected raw complete
  states and continuous traces for the 208 main and 50 auxiliary comparisons.

## Interpretation

Current-format state comparisons require exact equality. Historical save resume
preserves gameplay, orders and RNG streams, but 309 historical diagnostic state
differences remain; raw legacy-resume equality is explicitly false. Browser
scripting comparisons use the documented artifact normalization; canonical replay
and match trace comparisons use whole raw bytes. Chromium covers all four canonical
kinds; Firefox/WebKit cover replay and match plus their complete scripting corpora.

Strict ASan/UBSan uses leak detection and halts on diagnostics, with no suppressions.
Its 457 non-LAN engine cases, two held LAN engine cases and two separately typed
unit fixtures are distinct scopes. The two unit fixtures use the complete client
link to supply real RTTI; this does not claim a full lean-unit sanitizer suite.
Display coverage is documented separately in release evidence.

Windows built successfully but stopped after an inherited Bash helper error. The
master helper fix passes Linux CLI coverage; post-fix Windows execution remains
unverified. Twelve Windows replay traces match the native reference, while raw
CRLF/LF checksum sidecars are not claimed byte-identical. Android release/package
builds passed without device execution. Physical mobile/iOS execution and a
maintainer play-through remain unavailable. Original hosted failures stay visible;
this evidence does not claim the entire hosted workflow is green.

The current-master integration has actual closed execution and separate recovery
records. Interrupted-controller, scripting-output-directory, environment and
sanitizer-link failures retain their original status beside bounded corrections;
no interrupted or failed producer is relabeled as successful.
