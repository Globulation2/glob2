# Ground shadow specs

`specs.json` tells `tools/artwork/cast_shadows.py` how each building and
resource frame casts its ground shadow layer (`<frame>s`). See the tool's
docstring and [ground shadows](../../../docs/assets/lighting.md#ground-shadows).

Boxes are HD pixels of the frame (4× native): `blocks` are
`[x0, y0, x1, y1, height]`, `spires` are `[x0, y0, x1, y1, base_y, base_height]`.
Frames sharing a family entry (damaged variants) share its geometry. Review a
change with `python3 tools/artwork/cast_shadows.py --frames <frame> --review
artifacts/shadows`, then rerun the tool without `--review` to write the pack.
