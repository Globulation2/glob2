# Follow-up build contract repair (#775)

Tested commit: b6dd57b9c22ba649c47577c73a6423adfbe565e4.
Base: 598524bb0dbad2c796eaa6eb645c15ee54d380ac (merged terrain refactor #773).
Final fetched master matches that base; conflict-free merge tree: f092bf679a82554274bc33ff022eb73b76855256.

The original PR's cheap CI contracts finished failing as the merge completed. Retained failure log: ci-pr-773-failure.log. Two scene-header allowlist failures and the water backdrop's browser-package classification were introduced by moving terrain definitions into registries. The follow-up includes metadata-defined atlases/backdrops in browser game assets, permits immutable terrain definitions in Scene, and audits their transitive header dependencies. No C++ runtime or simulation behavior changes.

Local Linux x86_64 validation:

- `PYTHONPATH=test/build_system python3 -m unittest -v test_scene_boundary test_web_assets.WebAssetPlanTests`: exit0,13tests passed; build-contracts-focused.log.
- `"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system -v`: exit0,324tests run,2skipped; build-contracts-repair-encoder.log. This is the exact interpreter-selection command used by CI, preserving the asset-encoder process and mocks.
- `git diff --check`: passed.

An initial broad run using ambient Python instead of the pinned encoder interpreter failed eight unrelated packaging tests because the exporter re-executes into the encoder environment, bypassing in-process test mocks. Its log is retained as build-contracts-repair-ambient.log; it is not counted as a passing run. The correct CI invocation passes the full suite.

No full native simulation rerun is needed for this Python-only contract/packaging repair; original terrain evidence remains applicable to its unchanged C++ source. Hosted results are separate from this local evidence. The maintainer's merge authorization covers completing this focused repair.
