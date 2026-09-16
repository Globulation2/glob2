# The Glacis revision 2 review previews

Star forts in open country: bastioned walls, moat, covered way and a bare glacis ring against woods, streams with contested fords, kitchen gardens behind every courtyard. The design, controls and checks are in the
[framework document](../../map-generators/MAP_GENERATOR_FRAMEWORK.md).

- `revision1-256-4-seed7.png`: revision 1 (premade bases), 256x256, four colonies, seed 7.
- `revision2-256-4-seed7.png`: revision 2, same request.
- `revision2-mosaic-256-4-seeds1-4.png`: revision 2, seeds 1 to 4.
- `revision2-home-zoom-seed7.png`: colony 0's home in the seed 7 preview, four times enlarged.

Rotation tournaments (six 256x256 maps, four colonies, every cyclic rotation, 45,000 ticks,
revision 1 then revision 2 candidates, per colony-game): Nicowar: peak units 64 to 107, births 15 to 105, wheat 324 to 1,085, eliminations 33 to 11 of 96. Numbi (before one facing per map): peak 54 to 37, births 9 to 35, eliminations 2 to 14 of 96, with a facing bias that the single facing per map removes by construction. The later changes (Numbi
fixes, one facing per map) passed the shape sweeps, contract tests and golden rows but no
further tournament; a maintainer's playtest is the next evidence.
