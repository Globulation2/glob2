"""Convert the unmodified TRELLIS swarm for the compatibility OpenGL renderer.

blender -b --disable-autoexec --python tools/export_swarm_model.py -- REPO_ROOT
Preserves the source topology, UVs, smooth normals and embedded base-color map.
"""
import bpy
import hashlib
import json
import struct
import sys
from pathlib import Path

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
source = root / 'datasrc/gfx/buildings/swarm-trellis.glb'
output = root / 'data/models3d'
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(source))
objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
vertices = []
seen_faces = set()
duplicate_faces = 0
texture_bytes = None
for obj in objects:
    mesh = obj.data
    mesh.calc_loop_triangles()
    mesh.calc_normals_split()
    uv = mesh.uv_layers.active.data
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    for material in mesh.materials:
        shader = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        image = shader.inputs['Base Color'].links[0].from_node.image
        packed = bytes(image.packed_file.data)
        if texture_bytes is not None and texture_bytes != packed:
            raise RuntimeError('The runtime swarm expects one base-color map')
        texture_bytes = packed
    for tri in mesh.loop_triangles:
        face = []
        for loop_index in tri.loops:
            loop = mesh.loops[loop_index]
            point = obj.matrix_world @ mesh.vertices[loop.vertex_index].co
            normal = (normal_matrix @ loop.normal).normalized()
            # glTF Y-up is imported by Blender into the game's Z-up convention.
            face.append([*point, *normal, *uv[loop_index].uv])
        key = tuple(sorted(tuple(v[:3]) for v in face))
        if key in seen_faces:
            duplicate_faces += 1
            continue
        seen_faces.add(key)
        vertices.extend(face)

low = [min(v[a] for v in vertices) for a in range(3)]
high = [max(v[a] for v in vertices) for a in range(3)]
scale = 1 / max(high[0] - low[0], high[1] - low[1])
for v in vertices:
    v[0] = (v[0] - (low[0] + high[0]) / 2) * scale
    v[1] = (v[1] - (low[1] + high[1]) / 2) * scale
    v[2] = (v[2] - low[2]) * scale
with (output / 'swarm.g3t').open('wb') as stream:
    stream.write(b'G3T1')
    stream.write(struct.pack('<I', len(vertices)))
    for v in vertices:
        stream.write(struct.pack('<8f', *v))
assert texture_bytes and texture_bytes.startswith(b'\x89PNG')
(output / 'swarm-basecolor.png').write_bytes(texture_bytes)
metadata = {
    'source': str(source.relative_to(root)),
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'triangles': len(vertices) // 3,
    'bounds': [[min(v[a] for v in vertices) for a in range(3)],
               [max(v[a] for v in vertices) for a in range(3)]],
    'coordinates': 'Z-up, XY centered, base at Z=0, longest ground span=1',
    'texture': 'swarm-basecolor.png',
    'conversion': 'Original surface, UVs and embedded texture; exact duplicate triangles removed; coordinates normalized',
    'duplicate_triangles_removed': duplicate_faces,
    'texture_sha256': hashlib.sha256(texture_bytes).hexdigest(),
}
(output / 'swarm.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps(metadata, indent=2))
