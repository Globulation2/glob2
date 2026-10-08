# Final integration validation for PR 897

Tested source `0248fa5568ee81b84d0f7ec89291989cedaa9770` against integrated master `e17195d9cc141d521a920bbc3908115fc9648abf` (landscape resource catalog and travelling-water rendering). Linux x86-64, GCC15.2 release client, O3; freeze.json records compiler, flags, dependencies and executable hashes. Build with `CCACHE=1 GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/home/bradley/glob2-terrain-art2/build/linux/client/release/recording/prefix scons -j8 release=1 server=0 optimized_assets=0 tests build/linux/client/release/src/glob2`. Build logs and exact validation command JSONs are attached.

## Scope and integration

The preceding merge-review evidence validates cleanup head `8c1f6b7c9` against base `37213ca3c`, including all eight 1,024-tick fixtures matching archived `61b6ff740` at compute sizes 1 and 4. This demonstrates no cleanup-induced simulation change.

Master subsequently merged the landscape resource catalog as SIM34. Integrated it and the concurrent water rendering change, resolved only golden-fixture conflicts, and advanced this feature to SIM35. No production-code conflict required resolution. Regenerated golden traces for the combined simulation; refresh logs distinguish fixture generation from final verification without updates. Save/replay format 149, protocol 67 and durable save floor 58 remain unchanged.

Reviewed worker snapshot/RNG ownership, ordered owner publication, resource setters/accounting, snapshot/executor lifecycle, legacy save probes and replay/network gates. Historical readers and the immediate test/benchmark reference are intentional. No blocking architecture issue found. Cleanup expands the save-layout probe for readability, names the integrated format boundary, documents lineage discrimination and removes duplicate comments.

## Final verification

{
  "engine": {
    "passed": 608,
    "failed": [],
    "skipped": 46
  },
  "golden": {
    "passed": 13,
    "failed": [],
    "skipped": 0
  },
  "unit": {
    "passed": 891,
    "failed": [
      [
        "ImageAssets",
        "16-bit RGBA rounds normalized channels to the exporter reference"
      ]
    ],
    "skipped": 20
  }
}

The sole accepted unit failure is the pre-existing native SDL 16-bit PNG decoder mismatch, independently reproduced in prior baseline evidence. Native compatibility coverage includes the growth/save-layout/executor boundaries and integration with the new catalog. Golden checks run without fixture updates after the explicit refresh. Maxima verifies 512 complete baseline ticks plus 256 midpoint save/reload ticks at compute sizes 1 and 4. Legacy telemetry fresh loads and the v108 checkpoint are likewise checked at both sizes. Simulation-version and whitespace gates pass.

Eight fixed 1,024-tick scenarios compare exact per-tick world/replay checksums at compute sizes 1 and 4. continuation.json also records comparison with the pre-integration cleanup build; all 16 comparisons match the pre-integration world and replay traces as well as the final zero-worker reference. This is exact equivalence on these frozen inputs, not a claim that newly generated maps ignore the new catalog.

## Limits

No new Windows/macOS/Android/browser/threadless-build/display execution, gameplay review or performance measurement. Runtime zero workers does not establish a threadless build. Prior historical timing, slow-tick and memory regressions remain explicit in the PR; the user accepted those tradeoffs and requested merging. Existing running master CI is left untouched.
