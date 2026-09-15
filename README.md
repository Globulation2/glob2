# Maxima runtime fixes: review evidence

Source revision is in source-revision.txt. Final regression logs were rerun after merging master 12bf1f3e8. Earlier behavioral comparisons were recorded at the individual fix commits.

## Upgrade preference

The input save starts at tick 120079. With baseline 4f98ba844, first upgrade selection was tick 137888; with 122221693 it is 122037 (15851 ticks earlier), with actual completion at 126300. Further completions: 130657, 135026, 139368. See upgrade-comparison.json and upgrade-before/after.log.

To reproduce, decompress upgrade-input.game.gz, build either revision, and run from the repository:

```
build/src/glob2 --run-game --load-game /absolute/path/upgrade-input.game --ticks 140000 --telemetry maxima --save final --output-dir /absolute/path/results
```

The original argv files include the original machine paths. upgrade-after.game.gz is the resulting save.

## Training and food

Fingerprint map seed 1552288803, game seed 1760225446: seven attack-eligible warriors were blocked by nine nominal training slots, although none could learn. TrainingGateProbe.cpp, InspectCombat.cpp and combat logs document the before/after diagnosis. fingerprint-input.game.gz preserves the input.

food-before-after.json records same-seed Anthill and Switchbacks runs before/after the combined training/food fixes. jobs.json and batch-results.json retain generated-game settings and outcomes. These demonstrate recovered growth, not general playing strength.

## Checks and limits

final-tests.log contains combat, economy, implementation and placement regression results. final-relocation.log covers relocation handover and replacement loss. Earlier logs are also included.

checksums-a.gz and checksums-b.gz are detailed checksums from repeated loads of the same Fingerprint checkpoint, run for 1000 further ticks with the training/food fixes. checksum-comparison.json records comparison. This checks repeated-load determinism on macOS arm64 only. Windows/Linux per-tick equivalence is unverified. An existing full-engine uninterrupted-vs-save/resume discrepancy reproduced on the previous build remains unresolved; these repeated-load checks do not establish uninterrupted equivalence.

Save format 107 preserves fractional food and replacement links, with version-gated older loading and save floor 58 unchanged. AI behavior changes can alter replayed AI decisions and mixed-version games; unchanged acceptance gates do not establish behavioral equivalence.

The intentional 50% upgrade bonus shifts Maxima toward earlier technology; food/training fixes increase growth and aggression in affected situations. Human gameplay review remains valuable.
