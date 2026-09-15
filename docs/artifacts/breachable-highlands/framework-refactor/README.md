# Shared primitives extracted from Breachable Highlands

The bulk study prompted a framework refactor, with generator revision 4 retained.
Four existing modules now provide the reusable operations:

| Module | Operation | Contract / reason |
| --- | --- | --- |
| GraphMaze | `closedEdges`, `edgeDetours` | Enumerate eligible crossings in edge order, then score the route they bypass. Cache one BFS per source cell; consume no randomness and preserve separate parallel edges. |
| Tessellation | `cellCrossing` | Cross a specific edge through its midpoint, stopping outside reserved centre discs. Use the edge's unwrapped frame so a two-cell torus has two distinct crossings. |
| Topology | `labelComponents` | Assign an owner to each connected component and return the first conflict. Unlabelled ground still connects labelled areas; callers choose walking or future-growth passability. |
| Walls | `checkGatePartition` | Seal all explicit plugs, detect boundary leaks, then prove each connected plug touches its two intended regions. Include diagonal/wrapped movement and reject accidental third-region connections. |

Breachable Highlands now composes these operations instead of implementing its own
crossing geometry, graph floods and gate/component checks. Its source shrank from
543 to 469 lines. Valley budgets, reserved farm space, resource quotas, saddle
selection policy and playable-map thresholds remain documented in the generator
and [design reference](../../../map-generators/BREACHABLE_HIGHLANDS.md).

The gate validator adds an explicit third-region check. The extraction otherwise
preserves arithmetic, edge order, seeded tie order and random consumption. No
existing generator was switched to a new algorithm, and no simulation, save or
network version was changed.

## Verification

The shared toolkit fixtures exercise disconnected shortcuts, parallel torus edges,
protected endpoints, reserved centres, unlabelled bridges, diagonal and seam leaks,
empty/disconnected plugs, wrong endpoints and third-region contact. Existing golden
maps are checked without updating expected hashes.

[plan.py](plan.py) selects paired baseline/refactor jobs through the existing
`tools.tournaments` framework: every supported topology at one seed, every crossing
interaction, held-out and ordinary endpoint corners, and all metric extrema.
[compare.py](compare.py) requires identical complete native map reports and SHA-256
hashes of saved map bytes for each pair. The immutable baseline is the executable
used for the [3,726-request bulk study](../bulk-generation/README.md); the refactor
has a separate bundle identity. This distinguishes bulk evidence from checks of the
subsequent source extraction.

### Results

- Shared toolkit and generator contracts passed on macOS arm64 and Linux x86_64.
- All **256 golden rows per platform** passed without updating expected hashes:
  [macOS](goldens-macos.log), [Linux](framework-goldens-linux.log).
- [Complete paired records and attempts](framework-reports.zip), [retention audit](framework-retention.json).
- **613 paired worlds** have identical full native reports and byte-identical saved
  maps: [comparison and per-map hashes](framework-comparison.json).
- [Source snapshot and patch](source.zip), [Linux source hashes](framework-source.json),
  [macOS source hashes](source-macos.json), [immutable comparison build](framework-bundle.json).
- Native tail maps and previews: [least room](previews/least-room.png),
  [lowest fairness](previews/least-fair.png), [longest rival route](previews/longest-rival-route.png).
  Their `.map` files and complete reports sit beside the PNGs; [index](previews/index.json)
  records exact jobs. Visual inspection found intact ridges, contained farms and
  clear home aprons; the last map visibly has long winding routes.

### Save-rotation tool defect found and fixed

The paired run requested saved maps, which also exercises the study tool's full
team-rotation cycle. **150 jobs (75 pairs)** with 11 or 12 colonies reported an
artifact failure in both baseline and refactored executables. Their ordinary
save/reload was idempotent, entity references and world/colony checks passed, and
baseline/refactor r0 maps and reports were identical. The failed assertion was
only the complete rotation round trip. These are explicitly retained as failed
attempts, never counted as accepted successes by the coordinator.

The cause was the label-length transition at `r9` → `r10` and back to `r0`:
`Game::save` hashes the first header before patching the changed map offset.
`saveRotatedMaps` now primes the offset when the label length changes before
producing the canonical bytes. This changes only study artifact normalization;
it does not change the save format or gameplay. The extra comments explain why
this write is needed. Existing r0 files remain byte-identical.

A separate final build passed complete **10-, 11- and 12-colony** cycles, writing
and checking all **33 rotated maps**, with original-world and colony signatures,
reference consistency, idempotent reload and byte-for-byte round trips intact.
[Regression verification](rotation-verification.json), [full reports](rotation-reports.zip),
[all 33 maps](rotation-maps.zip), [final build](rotation-bundle.json),
[final source hashes](rotation-source.json). The macOS client also rebuilt after
this tool fix; the rotation regression itself ran on Linux.

The paired study initially retried these artifact failures under the standard
infrastructure budget. After confirming their deterministic cause, its coordinator
was resumed with an infrastructure-attempt limit of one to avoid redundant retries;
already queued work was collected. All attempts remain in the ledger and review
archive. The comparison script explicitly verifies the rotation-only failure flags
before comparing these pairs. Windows was not tested. Human play and late-game
balance remain covered only by the separate playtest study's stated limits.

All 16 study workers were stopped after their attempts were acknowledged; the
[shutdown record](worker-shutdown.json) retains the final per-worker counts.
