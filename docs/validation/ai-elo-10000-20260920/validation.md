# Final UI validation

Validated on macOS after merging master through `1816a36f5`, using the final 10,000-game ratings:

```sh
scons -j8 release=1 server=0 custom-setup-test
./build/src/CustomGameSetupHarness
./build/src/CustomGameSetupHarness artifacts/pr364-10000-ui ui
./build/src/CustomGameSetupHarness artifacts/pr364-10000-ui ai-profile
python3 docs/validation/ai-elo-10000-20260920/reproduce.py
```

The release build, [regression harness](validation-harness.txt), [all three interactive launch flows](validation-ui.txt), and [focused profile capture](validation-profile.txt) exited zero. The [fresh screenshot](../../win-probability/ai-strengths.png) was visually inspected: ratings, ordering and labels fit.

Independent standard-library reproduction checks all 10,000 unique games and 5,000 two-game blocks, and matches every final pooled score and 95% bootstrap endpoint within 1e-8 Elo. The final exporter additionally checks the exact 60-generator quotas and all 18 replacement links, retaining the original schedule positions.

The broader macOS visual suite previously hit a map-preview zoom assertion outside the selector changes; it was not rerun or claimed to pass locally. This UI PR changes no simulation code or serialized AI IDs. No fresh cross-platform per-tick simulation-checksum comparison is claimed. Linux/Windows CI results are linked from the PR.
