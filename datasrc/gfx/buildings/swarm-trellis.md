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

The colony-skins conversion is `tools/skins/export_swarm.py`, run with pinned
Blender 3.6.23 and `--python-exit-code 1`. It welds a working copy, reconstructs a closed surface with sharp octree remeshing, simplifies to
approximately 2,000 triangles and makes a new UV layout for painted skins. The
source textures are retained here for provenance, but are not copied into the
new skin. Runtime output and its source/content hashes are generated under
`artifacts/skins/units`; the original GLB remains unchanged.

This source was recovered from the `codex/glob-3d` experiment. It is a generated
interpretation of the original swarm sprite, not an original Blender model.
Camera alignment and simplified silhouette still need in-game review.
