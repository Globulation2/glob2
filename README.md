# PR 754 verification evidence

Tested repair head: 0bd8c34e65e070a9953ba1d8472dcd920e0c47ec
Base: 99958b81c2541fde7789c67451dae441ec24e93a
Final fetched master: ec99d5aface976eec14941b772eaaecfe55ba6ec (only unrelated platform skin-studio theme changes).

Linux x86_64; exact GCC 13.3 hosted binaries from master run 37255462043 artifact linux-test-programs-ubuntu-24.04 (11323395713). Source revision embedded in the binaries is 99958b81c2541fde7789c67451dae441ec24e93a. Repair changes archives/container packaging only, so these unchanged programs test the repaired transfer directly. SDL 3.4.16 and all shared dependencies come from that same hosted artifact. Hosted compiler flags: -std=gnu++20 -Wall -fPIC -O3 -s. Python 3.14.4; pinned private encoder Pillow 12.2.0/libwebp 1.6.0. Docker 29.1.3.

## Commands and results

Before repair: UIIcons/all semantic icons have the pinned raster sizes fails when the hosted program is pointed at source artwork (webp-ci-before.log).

`python3 tools/package_assets.py --platform linux --output artifacts/native-export-99958`: successful complete export, 4535 files; runtime-assets-audit.json.gz and runtime-assets-sha256.txt retain recipe and byte evidence.

`python3 artifacts/webp-ci-transfer/verify-transfer.py`: executes literal archive commands extracted from both repaired workflows, restores both archives, compares every asset byte and audit, then tests the restored Linux program. Both archives restore all 4535 files byte-identically. Script and archive commands included. Set GLOB2_TEST_SOURCE_ROOT to the relocated checkout, GLOB2_ASSET_DIR to the restored tree, LD_LIBRARY_PATH to the restored hosted SDL prefix. No runtime code or test assertion changed.

15 focused native render/runtime-pack/icon/cache/diagnostic tests pass, no failures/skips (130.0 seconds). Eight CLI smoke tests pass (3.197 seconds), including generated-map preview, complete tick records, and save continuation. See render.log and cli.log for selected cases.

`/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -v`: 323 tests, one skip, passed. After final Docker edits: same Python with `-p test_ci_policy.py`: all 19 pass.

`docker build --target cpp-deps -t glob2-ci-webp-deps:repair-754 -f deploy/Dockerfile .`: passed; image sha256:5314cca273b162ccf6aaa8fec754cf726af4b59d9017d8643134445c93968c39.
`docker run --rm -v "$PWD:/source:ro" glob2-ci-webp-deps:repair-754 python3 -c 'from tools.package_assets import encoder_python; import subprocess; interpreter=encoder_python(); subprocess.run([str(interpreter), "-c", "from PIL import Image, features; print(Image.__version__); print(features.version(\"webp\"))"], check=True)'`: passed private venv creation/Pillow probe. Full output retained.

## Coverage and limits

Transfer round trips directly cover the lost assets without rebuilding unchanged engine sources. Container dependency build and encoder probe cover missing ensurepip; staging uses the same audited runtime export rather than raw source artwork. Full container engine/stack, Windows, macOS, Android and complete native/browser matrices were not run locally. Hosted affected verification is requested and remains in progress. Independent Windows encoder bootstrap failure and intermittent browser first-frame failure remain separate repairs; this PR does not claim full master recovery. No simulation behavior, format, version gates or visual assets changed.
