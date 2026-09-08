# ADR 005: generation randomness before cooperative scheduling

Status: explicit seeds and cooperative editor generation implemented; long helper operations remain.

Generation previously mixed the synchronized generator with libc `rand`,
time-based reseeding, and shared static Perlin lookup tables. Consequently,
creating/reseeding a noise helper could change existing noise objects, including
cloud rendering. A caller's seed alone could not reproduce a generated map.
Yielding this work would expose it to still more unrelated RNG activity.

Each `PerlinNoise` now owns its lookup tables and a standard MT19937 generator.
Explicit reseeding is repeatable and does not touch libc RNG state or another
noise object. The zero-length normalization case produces a finite unit vector.
Height-map generation seeds its own noise objects from the synchronized stream;
terrain/resource placement uses that stream too, without internal time reseeding.

`MapGenerator::generateMap` accepts an explicit seed, applied before generation
work begins. The existing overload chooses a time-based seed for ordinary user
requests. `EditorGenerateScreen` retains its chosen seed and restores the prior synchronized
RNG state on cancellation, just as loading screens do.

This intentionally changes newly generated layouts: old generation did not have
an isolated, reproducible seed-to-map contract. Existing map/save files and their
simulation rules are unchanged. This does not yet establish bit-exact generation
across native/Wasm platforms; floating-point height-map calculations still need
cross-platform qualification. Recorded maps remain the simulation fixtures.

Native tests verify noise-instance independence, explicit reseeding, finite
noise samples, and no libc RNG mutation. All nine generation methods repeat their game checksum and final synchronized RNG
state when synchronous execution is compared with scheduled execution interleaved
with unrelated noise construction and libc RNG draws. Cancellation at several
checkpoints restores RNG state; invalid dimensions fail without publishing a map.

## Cooperative ownership and remaining latency work

`MapGenerator::generateMapTask` and its terrain/team subtasks yield through the
shared cooperative-task abstraction. Existing synchronous APIs drain those same
jobs. Callers must retain the generator, game, and descriptor until completion or
destruction. `EditorGenerateScreen` owns the descriptor in its coroutine frame and
the partial editor through the preparation screen; only successful completion
transfers that editor into an editing session. Cancelling destroys the suspended
job before the partial editor and restores the prior RNG state. Generation errors
return to an owned in-game message and the new-map flow.

Checkpoints cover generation stages, selected terrain loops, and terrain sprite
regeneration columns, preserving traversal and RNG order. Height-map, gradient,
allocation, and other helper calls still contain synchronous work: this does not
yet guarantee a maximum callback duration. Further subdivision and measured
large-map latency gates are required before removing Asyncify.

Fruit placement now limits random retries, scans for an eligible tile, and fails
if none remains, avoiding an endless loop. The expanded fixtures also exposed an
old-islands building-placement call using team ID -1; it now uses the actual team.
These fixes apply to native and browser execution alike.
