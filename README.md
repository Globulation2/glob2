# PR 757 encoder bootstrap verification

Head b3e48725fc6904df52bb6748c30c29dc8f45cc0c; base e7f249f41a5f354f696e9d65abc0a243a4556435. Linux x86_64, Python 3.14.4, GCC 15.2 host Python; private Pillow 12.2.0/libwebp 1.6.0.

`/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -p test_ci_changed_paths.py -v`: 30 pass.
Same command with `-p test_ci_policy.py`: 19 pass.
Set GLOB2_ASSET_ENCODER_PYTHON to that private interpreter, run `python3 tools/package_assets.py --encoder-python`, then `python3 tools/package_assets.py --source artifacts/mingw-encoder-external-export/source --output artifacts/mingw-encoder-external-export/export --platform windows` with a generated 16x16 RGBA PNG. System Python lacks Pillow, so this exercises external interpreter probing and actual worker subprocess export. One verified WebP exported, exact pinned encoder selected.

Actual Windows hosted verification: run 37259325633/job111603582115. Prepare pinned asset encoder passed at 03:30:01 UTC on final PR head; engine build/testing is still running. This is the missing standard-CPython setup, not a test expectation change. The release workflows already prepare standard CPython separately from MinGW. Actual Windows/MSYS venv layout and binary-wheel restriction are documented by https://www.msys2.org/docs/python/ .

Full engine and platform matrices omitted locally because only Windows CI interpreter setup changes. No simulation, asset recipe or loader source changes.

## Integration refresh after PR 758

PR 758 changes pinned SDL dependency inputs. Rebased the Windows setup repair to that current master to request fresh hosted integration. New head 52872ea50324840462dbf1a9fcd8a66572c59896; base 42df42802f0a7ed8729fb221d5de419657aed2b3. Same 30 changed-path and 19 CI-policy checks pass again; separate pinned encoder probe and actual Windows asset worker export pass again (mingw-encoder-external-export-refreshed.log). The earlier Windows bootstrap pass belongs to the previous revision and is not claimed as final integration coverage. PR remains draft awaiting the refreshed hosted Windows result.

## Final Windows integration checkpoint, October5 04:53UTC

Tested PR757 head52872ea50324840462dbf1a9fcd8a66572c59896, recorded hosted base42df42802f0a7ed8729fb221d5de419657aed2b3. Freshly fetched mastercd32f04ec30f3de1e9547723d9d29be675bf81ad has no intervening changes to .github/workflows, tools/package_assets.py or scons/sdl3_dependencies.py; the encoder/setup integration inputs are unchanged. Linux-host focused selector and external encoder tests recorded above remain applicable.

Actual Windows MinGW hosted job111606708547, run37260283911, now passes standard CPython setup and private pinned encoder preparation, pinned SDL build, engine/regression harness compilation, production CLI continuation checks, full unit-test step, shared JavaScript corpus and frozen simulation profile. Public job https://github.com/Globulation2/glob2/actions/runs/37260283911/job/111606708547; retained job API snapshot windows-progress.json and uploaded cli-windows files provide accessible checkpoint evidence. Compiler/toolchain metadata is in those CLI results/provenance. Commands/flags are exact workflow .github/workflows/build.yml at the tested head; setup-python3.12 delegates encoder creation via tools/package_assets.py --encoder-python, GLOB2_ASSET_ENCODER_PYTHON propagates to the unchanged MinGW build.

Engine tests, remaining maps/save/checksum checks and complete hosted final matrix are still running and are not claimed as completed. Known Linux/native coverage failures on this old base include PNG test SUBCASE inventory errors repaired by merged759; Chromium image count mismatch also repaired759. This13-line setup change does not modify test/simulation behavior. Focused actual Windows compilation, encoder execution, unit/CLI/JS outcomes establish the repaired integration; author accepts these results under AGENTS.md rather than awaiting every unaffected full-matrix job. Preserve all running master jobs.
