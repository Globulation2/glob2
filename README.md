# Evidence: faceted boulder decor

Review images for the boulder decor redraw (tools/artwork/terrain_decor.py).

- `ingame-before-after.png`: `glob2 map render` of `boulders-demo.map.gz` at native 32 px tiles, shown at 2x. Left uses master's decor frames, right uses this branch's.
- `boulders-hd-before-after.png`: the 12 HD boulder frames on the boulder ground, plus an 8x5 field drawn back to front.
- `decor-all-before-after.png`: the generator's `--sheet` output for all five decor sets. Hedge, thicket, ridge and outcrop change only through the shadow fix.
- `boulders-demo.map.gz`: FourSquares1 with two boulder regions painted in through `map export-image` / `map import-image`.
