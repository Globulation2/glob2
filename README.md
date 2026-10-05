# PR 757 encoder bootstrap verification

Head b3e48725fc6904df52bb6748c30c29dc8f45cc0c; base e7f249f41a5f354f696e9d65abc0a243a4556435. Linux x86_64, Python 3.14.4, GCC 15.2 host Python; private Pillow 12.2.0/libwebp 1.6.0.

`/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -p test_ci_changed_paths.py -v`: 30 pass.
Same command with `-p test_ci_policy.py`: 19 pass.
Set GLOB2_ASSET_ENCODER_PYTHON to that private interpreter, run `python3 tools/package_assets.py --encoder-python`, then `python3 tools/package_assets.py --source artifacts/mingw-encoder-external-export/source --output artifacts/mingw-encoder-external-export/export --platform windows` with a generated 16x16 RGBA PNG. System Python lacks Pillow, so this exercises external interpreter probing and actual worker subprocess export. One verified WebP exported, exact pinned encoder selected.

Actual Windows hosted verification: run 37259325633/job111603582115. Prepare pinned asset encoder passed at 03:30:01 UTC on final PR head; engine build/testing is still running. This is the missing standard-CPython setup, not a test expectation change. The release workflows already prepare standard CPython separately from MinGW. Actual Windows/MSYS venv layout and binary-wheel restriction are documented by https://www.msys2.org/docs/python/ .

Full engine and platform matrices omitted locally because only Windows CI interpreter setup changes. No simulation, asset recipe or loader source changes.

## Integration refresh after PR 758

PR 758 changes pinned SDL dependency inputs. Rebased the Windows setup repair to that current master to request fresh hosted integration. New head 52872ea50324840462dbf1a9fcd8a66572c59896; base 42df42802f0a7ed8729fb221d5de419657aed2b3. Same 30 changed-path and 19 CI-policy checks pass again; separate pinned encoder probe and actual Windows asset worker export pass again (mingw-encoder-external-export-refreshed.log). The earlier Windows bootstrap pass belongs to the previous revision and is not claimed as final integration coverage. PR remains draft awaiting the refreshed hosted Windows result.
