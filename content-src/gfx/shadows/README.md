# Ground shadow specs and mesh shadows

`specs.json` tells `tools/artwork/cast_shadows.py` how each building and
resource frame gets its ground shadow layer (`<frame>s`). See the tool's
docstring and [ground shadows](../../../docs/assets/lighting.md#ground-shadows).

- `mesh` (buildings): the committed mask in `mesh/<frame>s.png`, cast by
  `tools/artwork/mesh_shadows.py` from a TRELLIS model of the sprite, with its
  record `mesh/<frame>.json` (model, seed, mesh and input hashes, camera, gate
  metrics, shadow length). `length` is screen px of throw per px of height,
  art direction per building (default 0.6); `mesh` names another frame whose
  mask a damaged variant shares. Changing a length needs `mesh_shadows.py` again
  (casting only, no new meshes); `cast_shadows.py` refuses a mask cast at
  another length.
- `block` (building sites, wheat, rocks, and buildings whose meshes fail the
  gate): boxes are HD pixels of the frame (4x native), `blocks`
  `[x0, y0, x1, y1, height]`, `spires` `[x0, y0, x1, y1, base_y, base_height]`.
  Every mesh entry keeps its block geometry as the fallback.
- `canopy` (trees, fruit trees).

Rerun order after new base art: `trellis_meshes.py` for frames whose sprites
changed (staging only), `mesh_shadows.py --out content-src/gfx/shadows/mesh`,
then `cast_shadows.py`. Review lengths with `mesh_shadows.py --lengths DIR`.
