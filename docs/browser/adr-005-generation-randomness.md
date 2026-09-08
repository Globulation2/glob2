# ADR 005: generation randomness before cooperative scheduling

Status: implemented prerequisite; generation is still synchronous.

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
requests. Future generation screens must retain their chosen seed and restore
the prior synchronized RNG state on cancellation, just as loading screens do.

This intentionally changes newly generated layouts: old generation did not have
an isolated, reproducible seed-to-map contract. Existing map/save files and their
simulation rules are unchanged. This does not yet establish bit-exact generation
across native/Wasm platforms; floating-point height-map calculations still need
cross-platform qualification. Recorded maps remain the simulation fixtures.

Native tests verify noise-instance independence, explicit reseeding, finite
noise samples, and no libc RNG mutation. Uniform, swamp, islands, and concrete
islands fixtures repeat their game checksum and final synchronized RNG state
after unrelated noise construction and libc RNG draws. Other generation methods
and cooperative work budgets remain to be covered as the jobs are introduced.
