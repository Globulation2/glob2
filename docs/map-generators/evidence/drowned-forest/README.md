# Drowned Forest verification evidence

This bundle contains the complete Linux request/result tables, paired control audit,
map previews and playable map files, AI tournament summary, final profiling data,
review reports, and one replay with its matching macOS ARM64/Linux x86_64 per-tick
checksum sidecar. `manifest.json` records original artifact paths and uncompressed
SHA-256 hashes. Decompress `.gz` files with `gzip -dk FILE.gz` before loading them.
The review reports retain paths into the larger original development artifact tree;
not every diagnostic or game save is duplicated here.

## Results and revisions

- The frozen Linux study completed 793/793 control/extreme requests and 1,999/2,000
  random requests. All registered levels were compared on seeds 1–8. Four additional
  requests completed successful coverage of 336 size/colony/worker combinations.
- Seed 101727 failed the old 64-attempt limit at 128×256 with four colonies. The final
  dense rectangular cap is 256; the same request succeeds at attempt 103. The failed
  original row is preserved alongside the successful retest, not overwritten.
- The final exit-flood distance bound is exact for every goal/path the caller uses.
  Four map-byte comparisons and independent review support this optimization.
  Extending a search tail preserves every earlier successful result.
- The eight 25,000-tick games use seeds 7/19 and all four seat rotations of Nicowar,
  Cortex, Cabino and Maxima. The maps remain unchanged through the generator fixes.
  Development, late expansion and controlled worker harvesting were observed; human
  play and autonomous AI recognition of timber shortcuts remain unverified.
- `replay-comparison.json` records identical 4096-tick checksum sidecars from both
  platforms. One shared copy is included because the files are byte-identical.
  Playback uses the production loader via `replay-check.cpp`. Put the decompressed
  replay in a separate profile's `replays/game.replay`, then run that harness from the
  repository root with the absolute profile directory as its argument.
- `PROFILE.md` reports the final seed-7 benchmark: 512×512/eight colonies fell from
  15.529 to 10.003 CPU seconds. Difficult searches can take considerably longer.
- Historical evidence used provisional generator ID 58. Integration assigns ID 65
  because upstream allocated 58 during development. Named random streams depend on
  the seed and stream name, not this registration ID; integrated golden tests check
  the final registration. These are complete saved maps, not instructions to
  regenerate a different generator by its old ID.

## Reproduction

From the repository root:

```sh
scons release=1 server=0 -j4 map-generator-defaults-test map-generator-golden-test custom-setup-test build/src/glob2
build/src/MapGeneratorDefaultsTest /tmp/drowned-contracts --drowned-forest-only
build/src/glob2 --generate-map drowned-forest --seed 7 --width 256 --height 256 --teams 4 --output /tmp/drowned.map --preview /tmp/drowned.png --json /tmp/drowned.json
python3 .agents/skills/glob2-map-design/scripts/control_study.py drowned-forest ablation --binary build/src/glob2 --seeds 8 --teams 1-2 --jobs 4 --out /tmp/drowned-controls
```

Every random row records its complete seed, dimensions, teams and settings. Reproduce
it with `--generate-map drowned-forest`, those dimension/team arguments and one
`--set KEY=VALUE` per setting. Successful generation includes finished-world validation.
The study is not an exhaustive Cartesian product of controls. Mixed meadow-area and
mean worker telemetry limitations are recorded in `parameter-audit.json`.
