# Hiring K1/R4 verification

Tested commit: `edec427347a69a42f8a190354b70b414f2c6d876`. Base: `054804590ee1dd109f09642fc05654c324f08344`.
Linux x86_64, GCC 13.3; pinned existing SDL/recording/asset dependencies.
Build: dev_fast=1, linker=auto, -j12. Correctness testing, not performance measurements.

39 engine and 20 unit cases passed. This covers publication deadlines,
per-tick game/hiring checksum equality at 1/2/4/8 compute participants,
save round trips in binary/text at eight phases, shortlist ranking,
replay floor/ceiling, sim-version protocols, and golden match verification.
The binary/text phase saves are attached in save-roundtrips.tar.gz.
Current golden match and full per-tick verification trace are committed in the code PR.

Independent subagent review found no actionable findings. It confirmed the
committed patch matches the reviewed diff. Parent verified the published PR head.

Other native/browser platforms remain unverified. Existing four-tick hiring saves
are intentionally unsupported, explicitly authorized by the maintainer because
the policy was unreleased. There is no save migration layer.
Focused evidence accepted by the author under the repository self-merge policy
and the user's explicit merge instruction. No full hosted CI pass is claimed.

## Commands

```sh
export CCACHE_DIR=/home/bradley/glob2/artifacts/hiring-k1r4/ccache
export GLOB2_DEV_HOME=/home/bradley/glob2/artifacts/hiring-k1r4/dev-store
export GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix
export GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.12/venv/bin/python
python3 tools/dev_build.py --build=artifacts/hiring-k1r4/build engine-tests unit-tests -j12
python3 test/run_tests.py --build-dir artifacts/hiring-k1r4/build --binary engine --filter 'HiringPipeline/*' --filter 'HiringPlanner/*' --filter 'TurnEngineHarness/the committed*' --junit artifacts/hiring-k1r4/pr-engine.xml --artifacts artifacts/hiring-k1r4/pr-tests -j8
python3 test/run_tests.py --build-dir artifacts/hiring-k1r4/build --binary unit --filter 'HiringRanking/*' --filter 'ReplayStepCounter/*' --filter 'PlatformProtocol/*' --filter 'TurnMatchRecord/*' --junit artifacts/hiring-k1r4/pr-unit.xml --artifacts artifacts/hiring-k1r4/pr-unit-tests -j8
python3 test/check_sim_revision.py --base origin/master
git diff --check
```

All listed checks passed. Full initial successful build logs and the final
committed-head build log are included compressed. validation.json identifies binaries.
