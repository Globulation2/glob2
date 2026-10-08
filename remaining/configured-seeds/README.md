# Configured natural growth restoration

Tested source: `b1e8f5a3ca49df5b9fe52d2cd5622280d8fc140b`. Integrated base: `67fd5b935`; fetched master `1f216aaeb8b6f1fa2f74d8bb6e2763dde972a375` is not integrated. Vertex-terrain/save-format integration remains outstanding. This evidence covers Linux x86-64 only, GCC 15.2 release/O3. No Windows, macOS, browser, Android or threadless execution was verified in this follow-up. No performance claim is made; earlier timings used different seeding rules and proposal format.

Natural seeds again receive every configured initial material stock and source variety. Replenishment rates remain independent. Typed 12-byte pooled proposals precompute collision increments; publication draws no randomness. Save format148, replay floor148, protocol66, SIM33; save floor58 remains unchanged. Old pending unit proposals preserve their saved semantics through publication/resave.

## Reproduction and validation

Build command, executable hashes, dependency hashes and fixture source hashes are in freeze.json. Runtime commands below use `LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib`. Tests are run from the repository root. Build failures and initial fixture assertion failures are retained alongside final passing logs.

```
python3 test/run_tests.py --binary engine --no-display -j4 --filter 'ResourceGrowth/*' --filter 'ResourceGrowthBenchmark/player-free*' --filter 'TeamStatsSave/*' --filter 'ScriptCompatibility/*'
python3 test/run_tests.py --binary all --no-display -j4 --filter '*Resource*/*' --filter '*Gradient*/*' --filter 'WorldSnapshot/*' --filter 'SharedWorkerLifecycle/*' --filter 'AIPipeline/*' --filter 'AIOrderScheduler/*' --filter '*Replay*/*' --filter '*GameHeader*/*' --filter 'TeamStatsSave/*' --filter 'TurnEngine/*' --filter 'ScriptCompatibility/*'
python3 test/run_tests.py --binary engine --no-display -j4 --tag golden --update-fixtures
python3 test/run_tests.py --binary engine --no-display -j4 --tag golden
python3 test/check_sim_revision.py --base origin/master
GLOB2_GROWTH_PLAYER_FREE_OUTPUT="$PWD/artifacts/resource-growth/remaining/configured-seeds/ecology.json" python3 test/run_tests.py --binary engine --no-display -j1 --timeout 600 --filter 'ResourceGrowthBenchmark/player-free*'
python3 artifacts/resource-growth/remaining/configured-seeds/significance.py
```

Focused:35 passed,1 display skip. Broader JUnit:228 passed,1 display skip (runner schedules160 passing jobs). Golden:12 passed after fixture regeneration, simulation gate passed. Coverage includes configured multi-material seeds, variety, zero starting stocks, collision masks, publication-time rejection, legacy queued-output resave, invalid proposal fields, save/load, snapshot/gradient/cache and replay gates. Independent reviewer identified avoidable stateView ecology preparation; final code uses cellView. No confirmed simulation defect remained in that review.

## Ecology, not timing

360 runs:20 paired seeds ×6 scenarios ×immediate-reference/owner/shared. Uniform grass64²,512 ticks,delay8,owner1/shared4. Two passive teams, no players/units/buildings/harvesting. Every-tick owner/shared states and statistics match; stock scans reconcile statistics. Heavy checksums join pending work, so these runs cannot establish parallel performance. Reference is the retained immediate growth pass, not a latest-master executable comparison.

Mean final all-material stock differences versus immediate reference: dense−2.91%, sparse−5.84%, multi−5.46%, saturated−2.32%, blocked−0.24%, disabled0%. Multi was−16.69% with the previous unit-seed logic. The restored multi gap has paired bootstrap95% CI [−7.08%,−3.86%],100000 seed-pair resamples; exact paired sign test with Holm correction across six scenarios p=0.00001144. Full analysis explains endpoint and multiplicity limitations. Remaining differences cannot be causally apportioned between delay, original-snapshot decisions and RNG changes from this comparison.

Raw JSON, JUnit, logs and generated check artifacts accompany this report. Cross-platform determinism, refreshed reserved-core performance and latest-master integration remain draft follow-up work.
