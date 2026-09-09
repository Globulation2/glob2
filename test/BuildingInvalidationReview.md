# Reproduce the missing invalidations in PR #232

Based on PR head `5e8c1057fd20aab26a2f0c15396bf36214a78bfb`. This branch adds only a review probe, a build target, and these instructions; it does not fix the production code.

Run from the repository root, with the normal client build dependencies installed:

```sh
scons -j6 release=1 server=0 invalidation-review
python3 test/run-savegame-safety-tests.py --check-preferences build/src/BuildingInvalidationReview .
```

On a Homebrew Mac, prefix the second command with `DYLD_LIBRARY_PATH=/opt/homebrew/lib` if SDL cannot locate its libraries. No display, existing save, or user profile is needed. The runner creates disposable profile and working directories.

## Fixture

Each case uses a fresh 128x128 grass map, two teams, and a team-0 inn at (8,8). The obstacle is a team-1 inn. At tick 100, build the target inn's walking field. Add or remove the obstacle, then advance to tick 130, past the 25-tick dirty-rebuild throttle. Compare the normal cached field at the obstacle's origin with an explicit `target->dirtyGradients()` followed by `buildingGradient(target, 0)`.

1. **Placement:** start without an obstacle; place an inn at (12,8), then call the normal post-placement `addToStaticAbilitiesLists` and `update` methods. The target field remains clean and still treats the new footprint as walkable.
2. **Nearby combat destruction (positive control):** start with an obstacle at (12,8), build the field, then call `kill()`. The target is marked dirty and the normal lookup agrees with a fresh rebuild.
3. **Distant combat destruction:** same as case 2, but the obstacle is at (64,8), outside the 16-tile border. The target stays clean and keeps the now-empty footprint blocked.
4. **Nearby demolition:** obstacle at (12,8), then `launchDelete()` and the obstacle owner's `syncStep()`. This follows the ordinary demolition path rather than `kill()`. The other team's target stays clean and keeps the cleared footprint blocked.

Observed on macOS:

```text
place obstacle x=12: dirty=0 initial=65505 cached=65505 fresh=0 STALE
remove obstacle x=12: dirty=1 initial=0 cached=65505 fresh=65505 CORRECT
remove obstacle x=64: dirty=0 initial=0 cached=0 fresh=64985 STALE
demolish obstacle x=12: dirty=0 initial=0 cached=0 fresh=65505 STALE
PASS: disposable profile preferences unchanged
```

Here 0 means forbidden; the positive values shown are reachable distances encoded in the field. The probe asserts this known PR-head behavior, so exit status 0 means the reproduction succeeded, **not** that the PR's behavior is correct. The first, third, and fourth cases should become cached==fresh assertions in a final regression suite.

These demonstrate cache correctness failures, not a measured starvation or unfinished-construction regression in a full match.

## Why there is no save fixture

At this reviewed head, `Building::load` calls `freeGradients()` (`src/building/Lifecycle.cpp`), discarding the stale field and causing it to be rebuilt later. A save/reload would therefore erase the cached state being tested. The in-process fixture preserves the precise sequence of cache construction, topology change, throttled lookup, and forced rebuild.
