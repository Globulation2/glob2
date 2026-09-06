# TRELLIS swarm source

`swarm-trellis.glb` is the final local TRELLIS.2 swarm model from the asset-lab
experiment. The source GLB is retained unchanged, with its embedded 2048px
base-color and metallic/roughness textures. It has approximately 200,000
triangles and uses glTF Y-up coordinates.

The reference was the swarm image prepared from the original game sprite.
Generation used the local TRELLIS.2-4B model through the trellis-mac port, seed 42,
512 pipeline resolution, and 12 sampling steps in each stage. The final source
texture bake was 2048px. Source project: https://github.com/microsoft/TRELLIS.2
and Mac port: https://github.com/shivampkumar/trellis-mac.

The runtime conversion is separate and reproducible:

```sh
blender -b --disable-autoexec --python tools/export_swarm_model.py -- "$PWD"
```

The runtime conversion preserves the source surface, UVs, smooth normals and
embedded 2048px color texture. It removes exact duplicate geometric triangles
that cause rasterization speckles, then centers and normalizes the coordinates.
It intentionally retains approximately 199,000 triangles for this visual trial;
mesh simplification needs separate validation because naive decimation tears
the many UV seams. The original GLB remains unchanged.

The `G3T1` runtime mesh stores triangle vertices as eight little-endian floats
(position, normal, UV), following a four-byte magic and a 32-bit vertex count.
It uses Z-up, a centered XY base at Z=0, and a longest ground dimension of one.
`data/models3d/swarm.json` records the source and texture hashes, actual triangle
count, bounds and duplicate removal count.

The compatibility renderer uses the baked texture's brightness with the unit
team palette because the generated green model has no separate team-color mask.
It does not use the source metallic/roughness map. Completed swarms and swarm
placement previews use this solid model; construction sites retain their
original sprite. Other building types and the 2D renderer retain their sprites.
