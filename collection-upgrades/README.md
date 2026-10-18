# Policy-independent resource-gradient collection

Source: `46392e5fa96d125d68c4531207ff54558c5359e4`, based on master
`3d896cbb95293da6d24caddd247a62c9ca99d77f`.

The collection workflow now uses seeded per-game/per-field interval mixtures,
separate planning reachability labels, immutable cohort contracts and source
snapshots, exploration/candidate role balancing, persistent map holdouts, and
checkpoint refresh-now/wait interventions. Prospective release-binary comparison
runs have probe-free CPU measurements and separate observer-equivalent quality
repeats. The engine's official model and demanded128-tick age cap remain unchanged.

Validation on macOS ARM64 / Apple Clang21, ordinary releaseO3 (no fast/PCH/unity):

- 36 Python tests, including C++/Python integer parity with UBSan.
- 25 focused native cases: policy/exploration, delayed publication, scalar-only
  observer labels, depleted sources, CLI constraints and save continuation.
- 4096 per-tick world/snapshot/full-replay vectors match the preceding official
  binary, one/four threads, and probe off/on.
- Explicit adaptive interval0 matches official vectors; fixed512 exploration
  matches across one/four threads over2048ticks.
- Matched save174 checkpoints retain2048+ overlapping world/snapshot rows.
- Final eight-game collection/audit/fit/resume smoke:94 movement episodes across
  eight maps,3751 planning queries, observed ages up to552ticks. Actual sampled
  interval strata were0,32,128,512. No high-demand old-field examples were observed;
  this is tooling validation, not adequate coverage for replacing the cap.
- All three selected-field branch arms executed; their actual admission ticks
  are retained. Unexecuted interventions fail the command.
- Same-binary prospective comparison checks runner order, accounting and observer
  equivalence only. Its timing ratios are noise and establish no CPU improvement.

Independent reviews approved after fixes for provenance relabeling, unexecuted
interventions, holdout movement across generations, one-shot fork continuation,
and common-field assignment correlation across games. A preliminary native CLI
fixture omitted required output-dir; it was corrected and the final25cases pass.

`evidence.tar.gz` contains exact commands, frozen source snapshots, plan/maps,
compressed episodes, native results/checksum vectors/saves, JUnit, support and
fit reports, split registry, branch and comparison smoke results. Paths in the
commands identify their original run locations; relocate inputs when reproducing.
`verification.json` provides hashes/build flags and explicit limits.

No new Linux/Windows/browser/GPU execution or manual gameplay verification was
performed for these collection upgrades. No new model is installed by the smoke
fit. The earlier full platform evidence remains in the reconciliation folder.
