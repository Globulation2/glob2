# Karst towers revision 2 review previews

River valleys among limestone towers: thickets, rivers between the rows of homes, terraced paddies,
doline lakes and gated home bowls. The design, controls and verification are in
[KARST_TOWERS.md](../../map-generators/KARST_TOWERS.md).

- `revision2-256-4-seeds-1-4.png`: 256x256, four colonies, seeds 1 to 4.
- `revision2-512-6-seed5.png`: 512x512, six colonies, seed 5.
- `revision2-control-extremes-seed3.png`: each control at its two ends (river meander, tower
  density, tower spacing, sinkholes; flooded terraces, lakes, paddy depth, river width), 256x256,
  four colonies, seed 3.
- `revision2-home-zoom-seed1.png`: a home bowl at seed 1, terrain close-up at 8 pixels per tile.
- `revision2-terraces-zoom-seed1.png`: river terraces and a ford at seed 1, 8 pixels per tile.
- `revision2-lake-zoom-seed1.png`: a doline lake and its fields at seed 1, 8 pixels per tile.

The full-map previews are the native map preview (`--generate-map ... --preview`); the close-ups are
`.agents/skills/glob2-map-design/scripts/render_terrain.py` over a terrain dump. Neither is an
in-game screenshot.
