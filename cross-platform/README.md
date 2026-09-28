# Additional final-head cross-platform verification

Baseline: `1c49596f5195188e7f078768f610454f7068d7c6` (original master).
Implementation: `c0c24b5825ade417b88bf9c1d431bbbf830c5aea` (final default lazy path).

## Results

Two fresh four-AI 256² games (Arena map seed 73 and Crater Lakes map seed 131; game seed 31), each 8,192 ticks, plus a 4,096-tick continuation of the exact same original-baseline checkpoint, ran on each host with both binaries. The AIs were Nicowar, Warrush, Cortex and Maxima. Maps and the canonical continuation input are retained under `inputs/`. Each host loaded the identical checkpoint bytes.

| Host | Toolchain | Baseline vs PR tick checksums | Against macOS tick checksums | Baseline vs PR saves |
|---|---|---|---|---|
| macOS arm64 | Apple Clang 21 | All 20,480 ticks identical | Reference | All byte-identical |
| therig Linux x86_64 | GCC 15.2 | All 20,480 ticks identical | All identical | All byte-identical |
| devlaptop Linux x86_64 | GCC 13.3 | All 20,480 ticks identical | All identical | Fresh games identical; continuation diagnostic caveat below |

This is 18 game executions / 9 paired comparisons / 61,440 compared tick pairs (20,480 scenario ticks repeated on three hosts). Linux builds and both baseline/final PathGradientHarness runs pass. Final-head oracle: 1,526 cases / 2,032,787 exact cells / digest 3281260807785418242. These are correctness runs, not additional controlled performance measurements.

The final-head CI run [36456275043](https://github.com/Globulation2/glob2/actions/runs/36456275043) passed. Its same-input 1,500-tick fixture traces from Ubuntu 22.04, Ubuntu 24.04, Windows and WebAssembly all match both macOS baseline and final-head traces, SHA256 `110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`. These traces and manifests are retained, not inferred from build success.

## Save-byte caveats: failures retained, not normalized

The comparison does **not** claim universal cross-platform save-byte identity. Raw failing saves are included in the manifest alongside the passing traces.

1. **Initial Echo fields.** Fresh initial saves differ across macOS/Linux before AI initialization. On therig, 21 bytes at offsets 3888258–3888278 differ, plus the save-header SHA1. This corresponds to `update_gm` and five alliance/vision integers in `Echo::save`. `Echo::Echo` does not initialize these members; `getOrder()` initializes them before gameplay uses them. Both baseline and PR produce the same bytes on each host. Fresh-game checkpoint and final saves match across platforms.
2. **Echo coordinate loading.** Loading the shared checkpoint swaps 15 stored x/y pairs between macOS and Linux, in both baseline and PR. The swapped list persists in the continuation final save; per-tick checksums still match throughout. `AddArea::load` and `RemoveArea::load` call `position(stream->readUint32("posx"), stream->readUint32("posy"))`, whose argument evaluation order is compiler-dependent. On therig the continuation initial save differs at 30 coordinate bytes at offsets 4423458–4423574, plus the header SHA1; final offsets are 4953065–4953181. This is a real pre-existing portability concern, not a claim that such differences can never affect a later game. These Echo files are unchanged between the compared commits.
3. **Maxima diagnostic read.** Devlaptop baseline/PR continuation saves differ only in `state.policy_bids_6.desired_warriors` sampled at tick 4096 (current sample initially, retained history sample in the final save), plus the header SHA1. `diagnose.py` parses the serialized telemetry and asserts that these are the only differing bytes. The adapter reads `policy_bids[6]` although `PolicyCount` is six, the previously documented out-of-bounds diagnostic read. Repeating the unchanged baseline also changes these bytes while all tick checksums remain identical. Original, PR and repeated-baseline outputs are retained. This PR does not fix this issue.

Consequently, these runs support unchanged tick-by-tick simulation for this PR on the tested inputs and platforms; they are not a universal mathematical proof or proof that existing save portability bugs are absent. Windows/WebAssembly full-game AI workloads and interactive play were not tested here; their additional coverage is the shared 1,500-tick fixture.

## Inspect and reproduce

`manifest.json` maps every compared save/checksum file to its unmodified bytes stored as `objects/<sha256>.gz`. Identical files are stored once; every host/variant has a manifest entry. Decompress with gzip and verify the listed byte count and SHA256. Inputs are ordinary uncompressed map/save files. `materialize.py` restores the run paths needed by `compare.py` and `diagnose.py` and verifies hashes.

`cases.json`, per-host `run-plan.json` / `results.json`, `run-games.py`, build logs, oracle logs and CI manifests preserve seeds, exact commands, compiler information, binary hashes and outputs. Source trees were exported with `git archive` at the two exact commits into isolated directories; no source changes or mode overrides were applied. Build each Linux variant with `scons -j8 release=1 server=0 build/linux/client/release/src/glob2 path-gradient-test` (therig used -j16). Then run `run-games.py --root <fresh-evidence-root> --base <baseline-binary> --head <head-binary> --base-cwd <baseline-tree> --head-cwd <head-tree>` with the retained cases and inputs in that root. Output directories must be new. All GLOB2 environment overrides are cleared and SDL dummy drivers used.

`comparison.json` reports literal file equality, including false save comparisons. The checksum assertions do not override those failures. The repeated baseline uses the original devlaptop continuation command with only a fresh output directory.
