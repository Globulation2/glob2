Tournament design follow-up verification

Commit fd1a046b89100f9145dbf8d90e76be0d5b0472c7. Tooling/documentation only; engine sources and committed model coefficients are unchanged from the earlier qualified PR revisions. Same Linux x86_64 GCC13 ordinary release binary.

- Default plan:256match slots,32each of1v1/3ffa/4ffa/8ffa/16ffa/2v2/4v4/8v8. Eligible pool:67non-editor generators and8nativeAIs from the engine catalog. Curated search controls, workers, square/rectangular64..512dimensions, independent map/game seeds and randomizedroster/alliance positions. Primarygeneratorquotas differ byatmost1. Up to32frozen generationcandidates per match; actual accepted coverage is reported separately.
- python -m unittest tools.test_resource_gradient_model tools.test_resource_gradient_tournament:16passed,0skipped on Linux. Covers reproducibility, generator/control/geometry compatibility, quotas, command/roster emission, retry only on structured map rejections, recovery, missing generator coverage, full/partial map overlaps across formats, missing-label strata, fit/export and4100C++/PythonUBSan prediction vectors.
- Plan-only command on the actual engine catalog generated256matches and its coverage report successfully. No256-game tournament was launched.
- Real generation:one sampled match for each of8formats, including large and rectangular maps. All8obtained playable maps in1..4deterministic attempts. Structured invalid requests in the catalog (joint size/colony/control constraints) are retained with commands/reports/logs; no failed simulation was converted to a training label.
- Final real collector smoke:4matches (1v1,2v2,3ffa,4ffa),4096ticks maximum,1concurrent game and1compute thread on devlaptop. All4completed and wrote compressed episodes, quality data, hashes, selected-map provenance and actual coverage. Exact generate/game commands are in each commands.json/generation-attempts.json. A second identical collection invocation resumed without rerunning accepted games.
- Final clean source revision and Python result are in the archive. Engine binary hash/provenance remain in manifests. The8-format generation smoke preceded the final reporting-only correction; the4collection games and final16tests ran on the clean final commit.
- Independent review:OK, including final actual-coverage correction and grouped split safeguards.

Commands (from isolated remote checkout):
python3 tools/resource_gradient_model.py plan --binary build/gradient-official/src/glob2 --output artifacts/diverse-plan.json
python3 tools/resource_gradient_model.py run --binary build/gradient-official/src/glob2 --plan artifacts/diverse-collection-plan-v2.json --output artifacts/diverse-collection-final --ticks 4096 --jobs 1 --threads 1
The actual run used separated CLI values: --ticks 4096 --jobs 1 --threads 1. Dataset paths and exact commands are retained in the archive.

No engine builds or platform determinism reruns were needed for these Python planning/data-splitting changes. The earlier PR's unverified platform/gameplay qualification limitations still apply. No retraining output replaced the committed model, and these instrumented smoke games are not CPU or gameplay-quality measurements.
