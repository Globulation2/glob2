# PR 1196: direct flight qualification

Source: `eddff43fbb57f772f570a9a23741ece5a6b4ecc5`, based on master `486a2230a4ddb257ba9dee34d3aace05f2260d6f`; prior PR head `522a89ff9`.

- Clean Linux x86_64 / GCC 13.3.0 development build of the game and both test binaries; `dev_fast=1`, `-O0 -g1`, no PCH/unity. No runtime dependency changes.
- 286 focused engine cases and 50 unit cases pass, with no failures, errors or skips. Coverage includes the nine new direct-flight cases, existing ablation golden, hiring/service/supplier policies, guard balancing, map edit/cache invalidation, released format126 migration, current save continuation, replay acceptance and the regenerated SIM-71 match golden.
- Ordinary 32-courier and constrained 64-flyer setups compare every per-tick aggregate and team/entity checksum byte for 128 ticks across 1/2/4/8 compute participants. Both resume identically at all eight publication offsets. Inputs, hashes, per-run commands, full sidecars and saves are in the archive.
- Static documentation, generated preparation rules, simulation-version and whitespace checks pass.

The archive retains exact commands, source/binary/dependency provenance, build logs, JUnit, case inventories, saves, checksum traces, and initial failures with their follow-up ledger. Initial fixture setup errors were corrected. The first broader run found an empty guard query overwriting invalid target coordinates; that production bug was repaired and the original ablation golden passes unchanged. Final results above are from the repaired clean revision.

No release performance measurement, hosted engine matrix, other-platform checksum comparison, GPU/NEON execution, visual testing or maintainer playtesting is claimed. Earlier PR performance results apply to the prior revision, not this patch.

Archive SHA-256: `b0cf11e51995676be3b12c286a0f97c3d3f98f2caba0b080dc2c041a107858d9`.
