# Thumb dial usability evidence

PR #592, source `73fa5b37462a26903a7176f8989782d66479300d`. The gameplay output includes clean compiler/source provenance. Baseline captures come from master `fe33142db145d3025bc28ea6c1ee52a3d96511eb` rebuilt before edits.

Linux x86_64, GCC 15.2.0, optimized SCons client with SDL 3.4.16 Linux SDK. All four GameGUITouch cases passed. Captures use synthetic touch input at 320×568 and 568×320; both thumb sides are covered. Before captures show the original selected-type ratio slider; after captures use a 50/25/25 production mix to show all three shares.

## Checked behavior

- Priority has exactly the same position for an ordinary building, swarm, and multiple flag types.
- Thin bands use nearest-ring hit testing with extra tolerance; production release on another ring cancels.
- Both proportion dividers commit a complete mix through the existing swarm order; dragging itself emits no orders.
- Stationary touches are no-ops; interruption and second fingers cancel.
- Each zero share can be restored, including when both dividers coincide.
- Production can be paused and restarted.
- Percentages total 100; proportion editing quantizes to 16 parts. The third share retains its normalized value when editing the other pair.
- Existing Spacious ratio increment/decrement, limits, pending order behavior, and simulation-state checksum checks pass.
- Existing worker/range/priority controls, touch scrolling, flag dragging, and scaled dialogs pass.

## Geometry

Ring-center distances in points, computed from the same layout constants (no safe insets):

| Viewport | Ring | Before | After |
| --- | --- | ---: | ---: |
| 320×568 | Workers | 174.4 | 188.8 |
| 320×568 | Production/range | 127.5 | 160.8 |
| 320×568 | Priority (swarm) | 80.6 | 132.8 |
| 568×320 | Workers | 166.4 | 172.8 |
| 568×320 | Production/range | 120.0 | 145.3 |
| 568×320 | Priority (swarm) | 73.5 | 117.7 |

Previously priority moved to a different ring for buildings lacking a production/range control. Now the three roles always occupy rings 0, 1, and 2.

## Reproduction

```sh
GLOB2_SDL3_PREFIX=<Linux SDL SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests
env -u WAYLAND_DISPLAY xvfb-run -a -s '-screen 0 1920x1920x24 -noreset' python3 test/run_tests.py --binary engine --filter 'GameGUITouch/*' --keep-profiles --artifacts artifacts/radial/after --junit artifacts/radial/evidence/junit.xml --verbose
```

`tests.log` and `junit.xml` record all four final cases. `gameplay-output.log` contains source provenance and orientation/hand checks. Baseline used the same build and test commands with only the actual-gameplay filter, before applying this PR.

Physical Android/iOS comfort, macOS and Windows have not been verified locally. Hosted checks are tracked on the PR. Existing ratios can round on the first proportion edit; Pause clears all shares and its next tap resumes worker-only production. These are intentional UI semantics; engine scheduling and save/replay/network formats are unchanged.
