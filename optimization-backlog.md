# Runtime resource optimization evidence audit

This is temporary follow-up evidence, not source documentation or a performance claim.

## Recovered inputs

- Merged revision: `9084ea907`. Comparing production `src`, `libgag`, `libusl`, `SConstruct`, and `scons` against `b3fab1a48` reveals no production engine changes. Differences are a generator test fixture and translated data text. This does not turn a b3fab build into an exact merged build.
- Retained b3fab executable: `artifacts/resource-preserved/candidate-b3fab1a48/glob2`, SHA256 `7de08ee1bcbb74b7d711efd29ad84237a499efc4626055d61fb1bab1db0fbbc9`. Full build/runtime provenance sits beside it.
- Historical f0 pure executable was SHA256 `842ca2aecc71c2cb48f9a26b146cd9c39dec3cab214deb260191719dc1592c00`; preserved symbolized twin is `8296540a82234e060250b1b6d8d445743e239f7049e1bd6c5219c4f6c36accab`.
- Historical f0 approved-fixes executable was SHA256 `eb48402f6332a0f519b7f6b6e6f908adc359f0c69ed5acc301efabffe3e46dc9`; preserved symbolized twin is `91d3543383785811fc1fbd79ee497d6cdf7c67201464383ba09f471fbf741741`.
- Both symbolized control provenance records identify only `.note.gnu.build-id` allocated-section differences. They are distinct executable artifacts, not byte-identical restorations.
- Data-only roots restored with `git archive f0ff8384b7e47d5867ff630ef8f3387591de3baa data` into `artifacts/resource-followup/control-f0-pure` and `control-f0-approved`. All 100 historically recorded runtime data hashes match for each. Original source patches and full historical provenance retained beside each root; recovery report is `artifacts/resource-followup/control-data-recovery.json`.
- The original 7e54 fixture-generation executable SHA256 is `c5d6d16ed9c3c0ec5c1e0260ba35830d94be3f887c42c3f7e26b62fc9664b84f`. Hash scanning all 51 surviving `glob2` executables under `/home/bradley` and `/tmp` found no match. Do not substitute f0 controls for original fixture generation.
- Authoritative priority saves: land-2-1002-maxima/checkpoint-24576.game.gz SHA256 `bad81fa2df4be1cbd9446bd6251c55468cc759bd9a25a147dc6e3aa0e2efd91a`; land-2-1001-cortex/checkpoint-24576.game.gz SHA256 `2a7be1f93c10462e5f73bb8c7c4fc2f9697a58a8ce7de9510dec8ee02d9cde0d`.
- Both exact timing windows load tick 24576 and stop at 32768, with compute threads 4, compute experiments ai, gradient workers 2, gradient delay 8. Original manifests preserved. Fixture regeneration is not recovery unless exact hashes match; any deliberate replacement requires a newly identified corpus and separate results.

## Ranking and already-rejected candidates

1. Cortex repeated nearest-Food queries merit fresh merged profiling. The pre-hard-space-cache exact09b profile attributes 10.30% flat user-cycle samples to byte-snapshot ordered ring scans. This is not evidence for another material cache: the merged pass already snapshots positive Food stock including secondary yields.
2. **Do not repeat the complete toroidal Food distance field.** Retained `pr846-retained/food-distance-diagnostics/land-2-1001-cortex-late/README.md` and frozen source show it was implemented, validated with 8192 identical per-tick GCS records, and withdrawn after instructions rose from 172.600B for the byte-snapshot intermediate to 181.968B (+5.43%). This instrumented diagnostic is not controlled end-to-end CPU acceptance, but it establishes construction cost and prevents treating that approach as untried. Only narrower lazy/adaptive query elimination would be a distinct candidate, and still needs profiling and controlled measurements.
3. The old exact09b profile attributes 17.16% to Map::checkTile, 14.17% placement, 10.69% ground BFS, and about20.66% propagation. Current merged Cortex already contains HardSpaceView. Its retained exact-command pair reduced instructions11.218% (168.331B to149.448B), with separate8192-record checksum equality. Reprofile before attributing those old costs to current engine.
4. Maxima old diagnostics show candidate/approved instructions ratio1.000433 vs approved/pure1.043034. Proposed discarded expansion-query elimination and donor-index hoisting have less than1% old self attribution; do not claim substantial expected savings.
5. No existing evidence justifies ecology mutex removal or mixed-stock storage redesign. Old GrowthCache::rate self attribution0.07%; Cortex growResources0.79%. Propagation37.82% in the older Maxima profile is intrinsic shared work without a demonstrated redundant resource query. Sampling percentages overlap inlining/startup limitations and are not predicted speedup.

No engine code was changed during this audit. Next step is a fresh exact merged-window profile, then a single measurable candidate only if attribution warrants it.

## Independent tooling review

Initial report-only, cleanup, validation and guide changes reviewed without blocking findings. Independent initial runner suite:12 passed. Governor/frequency additions reviewed;7 governor tests passed. Frequency boundary snapshots and perf permission probe correctly avoid claiming interval-effective frequency, which remains a separate required measurement or explicit omission. No timing campaign or governor mutation was run by this reviewer.
