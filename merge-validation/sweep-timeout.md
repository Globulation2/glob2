# Map sweep timeout diagnosis

Source: `8458d92fe62a87f6734d8385ffaa6b3e550d4727`.
Compiler: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0. Linux native release (`scons release=1`), existing
local SDL3 SDK. Generator code and golden harness match master eea70c0e1.

The unchanged shard 2/4 hit its 600-second cap; shard 3/4 passed. Repeating
shard 2/4 with a diagnostic 1500-second cap passed all cells with zero failing
combinations in 803.95 seconds wall time (797.64 user, 6.25 system). The final
CI sweep cap is 1200 seconds; golden-row checks retain 600 seconds. Coverage,
seeds, and shard concurrency are unchanged. This is a timeout repair, not a
claim of CI speed improvement or a hosted timing measurement.

Command, from repository root with the local SDK on LD_LIBRARY_PATH:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy /usr/bin/time -p timeout 1500s stdbuf -oL build/linux/client/release/src/MapGeneratorGoldenTest skins-merge-sweep-2-long --sweep 2/4
```

An ignored instrumented harness also ran only Portage Lakes (`--sweep 30/1000`)
and logged all 58 cases passing. It prints before/after each call without
changing generation. This helps explain the long shard but is not a replacement
for the complete, unmodified harness run above.
