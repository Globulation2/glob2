# Gradient integration for PR #791

Final source `785465c5d7b0ef8287fd7c811611d462c403f98b`, based on master `9bfee5aeedac41e12f771f7aa3f05f419fb9a2a1` after prerequisite #787. This includes the Trail rename, Markets V2 and audio scheduling changes in current master. The four optimization commits rebased without patch changes. Two fixture/benchmark identifiers now use Trail, preserving terrain ID4 and cost512, and queued regression coverage adds market/deferred sources. The benchmark keeps its historical `road` pattern key. Independent integration review found no blockers; production gradient/search files match the previously reviewed implementation.

## Results

- Full native release game and both test harness builds passed.
- **810 unit cases passed**, 17 skipped; **299 focused engine cases passed**, 23 skipped. Coverage includes ecology/terrain/gradients, Markets V2 goldens, save/replay/network compatibility, AI, and the new music producer/buffer/engine fixtures from master.
- **11 Python benchmark contracts passed**.
- **221 benchmark fixtures / 1,768 paired samples passed** against independent engine/strategic oracles and the frozen historical baseline. This one-repeat smoke run establishes integration correctness, not new performance estimates.
- **28,672 paired simulation ticks match exactly**, covering classic128 and mixed128 with Markets V2 enabled, normal growth, Maxima/Nicowar, seed12345, workers1/4 and gradient workers0/3. Full runs span4,096 ticks; each resumed run loads a baseline checkpoint at1,024 and continues through4,096. Baseline/candidate checksum streams, replay bytes and final save bytes match for every run. Serial/parallel streams match.
- Both executables reproduce the committed **701-tick network golden** exactly.

The comparison baseline is the freshly built ecology integration `a27016c593aa3d0f1784247f4081805e054fce2e`, which includes Trail/Markets V2 at master ca788, before the independent audio change. The candidate includes the subsequent audio master change as well as gradients. Binary/source/input hashes and all run commands are recorded in `gradient-provenance.json`, benchmark manifest and continuation manifest.

## Save-boundary header diagnostic

The first comparison intentionally stopped on the classic baseline's uninterrupted-versus-resumed global-checksum difference. Investigation identified a format-header difference, **not a continuation-state defect**: the input map has versionMinor134; saving writes current version135. `MapHeader::checkSum` includes that value, and `Game::checkSum` rotates it into global XOR `0x00800000` for this two-team/two-player fixture. This exact fixed delta holds for every one of the3,072 resumed ticks in both worker configurations; every team checksum and every recorded unit/building field is identical. The same header transition exists in the unoptimized baseline. The final runner preserves the raw diagnostic separately while requiring strict baseline/candidate equality, replay/save byte equality and worker equivalence. An independent diagnostic checks the derived header delta and equality of all remaining record bytes. Mixed-terrain Markets V2 has zero uninterrupted/resumed mismatches: the existing signed header-checksum arithmetic masks the same version bit with its required-terrain experiment hashes. The independent diagnostic reproduces that arithmetic explicitly. The stopped first attempt, complete comparison and header diagnostic are retained. No simulation behavior defect was found or hidden in this optimization.

## Reproduction and limits

Linux x86-64, GCC15.2. Build flags and dependency prefixes are unchanged from the preceding evidence. Full build command:

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/tmp/glob2-terrain-baseline-build/recording/prefix taskset -c 0-11 scons -j12 release=1 tests build/linux/client/release/src/glob2
LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib python3 test/run_tests.py --binary unit --no-display --exclude-tag benchmark --junit artifacts/gradient-optimization/merge-integration/gradient-unit.xml --artifacts artifacts/gradient-optimization/merge-integration/gradient-unit
LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib python3 test/run_tests.py --binary engine --no-display --filter '*Fertility*' --filter '*Ecology*' --filter '*Terrain*' --filter '*Gradient*' --filter '*Market*' --filter '*Save*' --filter '*Replay*' --filter '*Match*' --filter '*Maxima*' --filter '*Turn*' --filter '*Music*' --filter '*SoundMixer*' -j8 --junit artifacts/gradient-optimization/merge-integration/gradient-engine.xml --artifacts artifacts/gradient-optimization/merge-integration/gradient-engine
python3 tools/test_gradient_benchmark.py
python3 tools/gradient_benchmark.py --baseline-dir artifacts/gradient-optimization/baseline/src --output artifacts/gradient-optimization/merge-integration/benchmark-final --suite smoke --repeats 1 --cpu 12
LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib taskset -c 13,14 python3 artifacts/gradient-optimization/merge-integration/run-continuation-final.py
```

Historical baseline source is `3266c8e51`; see maintained benchmark reproduction instructions in `docs/development/reference.md`. The comparison script expects the two frozen executables named in its BINS mapping; build their specified revisions with the recorded dependencies. No display/manual play or non-Linux full-engine integration is claimed. Earlier scalar/SSE2/NEON under QEMU and native sanitizer evidence remains attributed to its original tested revision; real ARM performance remains unmeasured. No new performance claim is made for this combined revision: master adds an independent music worker. Original 11-pair whole-game measurements, memory tables and architecture evidence remain linked in the PR. No simulation version/API/serialized cache change or intended gameplay change.

## Hosted check limitation

[Hosted run37334940213](https://github.com/Globulation2/glob2/actions/runs/37334940213) failed the pre-existing browser-boundary contract: `test_shared_code_does_not_embed_browser_api_calls` detects an Emscripten include in `src/audio/SoundMixer.cpp`, introduced by the independent audio change on master. Both that source file and the boundary test are byte-identical to master `9bfee5aee`, verified in the attached provenance. The failed log is retained. This PR does not modify either file. Repository policy accepts focused local verification and permits merges with inherited master failures; no protection bypass is requested. Repair of the audio boundary belongs in its own focused change.

[Raw evidence archive](gradient-integration.tar.gz), SHA-256 `283e6bbcdb3407c319dad2de8dad20342206a8587135eca892d0a7cfaddc406f`. Extract at a separate repository root; its files stay under ignored `artifacts/`.
