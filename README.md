# Drowned Forest contract stack overflow repair

Final head2ccd51332e37da319616fcb0fa4ac2d4b5616b77, fetched base395d158a6e4980d617904d476e03c52416fd15d0.
Ubuntu26.04.1 x86_64/GCC15.2/Python3.14.4; release software-only engine harness, SDL3.4.16/image3.4.6/ttf3.2.2; encoderPillow12.2/WebP1.6. No production changes.

PR772 Windows job111687966343 (run37286569224) exits3221225725 (stack overflow) in MapGeneratorDefaults/Drowned Forest contracts. Retained crash diagnostics show ___chkstk_ms under chooseScoredSettlements -> selectedLayout -> generate -> GenerationService -> drownedForestContracts. The contract declares twelve Game variables, keeping many live concurrently while generation adds its own nested trial. Heap-own those test worlds, preserving request/seed choices, lifetime/destruction order and every check. Production generation, scoring, map serialization and simulation remain byte-for-byte unchanged; no SIM_REVISION change.

Old cached engine head6e57 (unchanged Game/contract/scoring files through master395d; source diff from a88master to395d empty) fails the same contract under a 1MiB process stack. Heap fixture working tree passes the SAME1MiB stack and complete envelope/repeatability/cache/growth/neck/worker regression checks. Final diff contains only the fixture header; production generation is unchanged.

Build:
```
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain engine-tests
```
Before/repaired working tree test:
```
(ulimit -c 0; ulimit -s 1024; build-software-terrain/test/glob2-engine-tests --test-suite=MapGeneratorDefaults --test-case='Drowned Forest contracts*' --no-breaks=true)
```
Final committed test plus existing explicit generator golden worlds:
```
(ulimit -c 0; ulimit -s 1024; build-software-terrain/test/glob2-engine-tests --test-suite=MapGeneratorDefaults --test-case='Drowned Forest contracts*,Explicit designs preserve pre-Random golden worlds' --no-breaks=true --reporters=junit --out=artifacts/settlement-stack/final.xml)
```

Windows/Wine/cross compiler unavailable locally. Linux bounded-stack verification addresses allocation directly; actual Windows platform recovery and larger MarketsV2 Game layout await hosted integration of this focused fixture fix. Feature PR772 itself is not merged or changed by this repair. Broad engine/simulation/platform matrices omitted for fixture-only storage change. Fresh engine build and existing golden/request checks cover affected fixture and integration. No assertions removed, options changed or stack limit raised.

Final committed validation PASS: 2 cases (existing golden worlds and full Drowned Forest contract), zero failures, 62.2879seconds, same1MiB stack.
