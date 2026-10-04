#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate paintable swarm mesh variants from metaball designs.

Run with Blender 3.6.23 --background --factory-startup -t 1
--python-exit-code 1 --python tools/skins/generate_swarms.py --
--output data/skins/colony-v1. Metaball polygonization is threaded and only
reproducible byte for byte with a single thread (-t 1).

Each design is a seeded list of metaball elements, the same primitive the
original glob units were modelled with, so variants share their soft house
style. Every variant uses one camera fit and stands on the same ground line in
the swarm's sprite canvas. Each is then scaled about its ground centre to cover
COVERAGE pixels of the 128px sprite, so variants carry the same visual weight;
the designs themselves are proportioned so their enclosed volumes also stay
close (the manifest records both).

Each design is written as swarm-<design>.gsk with a paint layout projected
along the game camera (see unwrap). Designs are the skin catalog's swarm mesh
ids: keep DESIGNS in the order of src/online/SwarmMeshCatalog.h and the
platform protocol's SwarmMesh (tools/skins/test_export.py checks this).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from swarm_metrics import SUPERSAMPLE, rasterize  # noqa: E402

CAMERA = (6, -8, 10)
HEIGHT = 1.7           # design volume: cylinder of radius 1, z in [0, HEIGHT]
CANVAS_FIT = 1.8       # projected design volume spans 90% of the clip square
DEPTH_RANGE = 0.5      # matches export_swarm.py
TRIANGLES = 2000       # matches the TRELLIS swarm's runtime budget
# Visual weight: every design is scaled about its ground centre until it covers
# this many pixels of the 128px swarm sprite, close to the TRELLIS swarm (5,486).
COVERAGE = 5200
SPRITE = 128
MAX_VERTICES, MAX_INDICES = 8192, 49152
THRESHOLD = 0.6


class Design:
    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)
        self.elements = []

    def ball(self, co, radius, stiffness=2.0, negative=False):
        self.elements.append(('BALL', tuple(co), radius, stiffness, None, None, negative))

    def ellipsoid(self, co, radius, size, axis=(0, 0, 1), stiffness=2.0, negative=False):
        self.elements.append(('ELLIPSOID', tuple(co), radius, stiffness, tuple(size), tuple(axis), negative))

    def chain(self, start, direction, length, r0, r1, bend=(0, 0, 0)):
        """Tapered limb of overlapping balls along a gently bent path."""
        start, direction, bend = (np.array(v, float) for v in (start, direction, bend))
        direction /= np.linalg.norm(direction)
        t, points = 0.0, []
        while t <= 1.0:
            radius = r0 + (r1 - r0) * t
            points.append(start + direction * length * t + bend * t * t)
            self.ball(points[-1], radius)
            t += max(0.35 * radius / length, 0.02)
        return points[-1]

    def spire(self, base, height, radius, lean=(0, 0)):
        tip_direction = (lean[0], lean[1], 1.0)
        return self.chain(base, tip_direction, height, radius, radius * 0.18,
                          bend=(lean[0] * 0.2, lean[1] * 0.2, 0))

    def jitter(self, scale):
        return float(self.rng.uniform(-scale, scale))


def crown(d):
    """Closest to the original sprite: spires ringed around a central egg."""
    d.ellipsoid((0, 0, 0.16), 0.64, (1.0, 1.0, 0.7))
    for k in range(18):
        a = 2 * math.pi * k / 18
        d.ball((0.8 * math.cos(a), 0.8 * math.sin(a), 0.02), 0.2)
    count = 7
    for k in range(count):
        a = 2 * math.pi * (k + 0.5) / count + d.jitter(0.12)
        r = 0.82
        height = 0.95 + 0.5 * abs(math.sin(k * 2.3)) + d.jitter(0.1)
        d.spire((r * math.cos(a), r * math.sin(a), 0), height, 0.3 + d.jitter(0.03),
                lean=(0.18 * math.cos(a), 0.18 * math.sin(a)))


def clutch(d):
    """A clutch of brood eggs nested in a low rim."""
    for k in range(26):
        a = 2 * math.pi * k / 26
        d.ball((0.84 * math.cos(a), 0.84 * math.sin(a), 0.04 + 0.03 * math.sin(3 * a)), 0.19)
    d.ellipsoid((0, 0, -0.08), 0.8, (1.0, 1.0, 0.25))
    eggs = [((0.0, 0.0), 0.4, 0.0)] + [
        ((0.56 * math.cos(a), 0.56 * math.sin(a)), 0.3 + 0.03 * (k % 2), 0.35)
        for k, a in enumerate(2 * math.pi * k / 5 + 0.3 for k in range(5))]
    for (x, y), radius, tilt in eggs:
        axis = (tilt * x + d.jitter(0.08), tilt * y + d.jitter(0.08), 1.0)
        d.ellipsoid((x, y, radius * 1.1), radius, (0.85, 0.85, 1.4), axis=axis, stiffness=4.0)


def toadstool(d):
    """A broad-capped toadstool with a ring of young caps at its foot."""
    d.chain((0, 0, 0), (0.05, 0.02, 1), 0.85, 0.3, 0.22)
    d.ellipsoid((0.05, 0.02, 1.05), 0.78, (1.0, 1.0, 0.38))
    d.ellipsoid((0.05, 0.02, 0.86), 0.5, (1.0, 1.0, 0.25), stiffness=1.5, negative=True)
    d.ellipsoid((0, 0, 0.0), 0.55, (1.0, 1.0, 0.3))
    for k, a in enumerate((0.4, 1.9, 3.3, 4.6)):
        a += d.jitter(0.2)
        x, y = 0.72 * math.cos(a), 0.72 * math.sin(a)
        height = 0.22 + 0.1 * (k % 2)
        d.chain((x, y, 0), (0.15 * math.cos(a), 0.15 * math.sin(a), 1), height, 0.1, 0.08)
        d.ellipsoid((x * 1.05, y * 1.05, height + 0.04), 0.2, (1.0, 1.0, 0.5))


def coral(d):
    """Branching spires that fork from a low mound."""
    d.ellipsoid((0, 0, 0.0), 0.75, (1.0, 1.0, 0.4))

    def branch(start, direction, length, radius, depth):
        tip = d.chain(start, direction, length, radius, radius * 0.7)
        if depth == 0:
            d.chain(tip, direction, length * 0.45, radius * 0.7, radius * 0.15)
            return
        base = np.arctan2(direction[1], direction[0])
        for side in (-1, 1):
            a = base + side * (0.7 + d.jitter(0.25))
            spread = 0.42 + d.jitter(0.08)
            child = (spread * math.cos(a), spread * math.sin(a), 1.0)
            branch(tip, child, length * 0.7, radius * 0.72, depth - 1)

    for k, a in enumerate((0.3, 2.4, 4.3)):
        a += d.jitter(0.2)
        start = (0.3 * math.cos(a), 0.3 * math.sin(a), 0.1)
        branch(start, (0.25 * math.cos(a), 0.25 * math.sin(a), 1.0), 0.5 + 0.08 * k, 0.24, 2 if k else 1)


def skep(d):
    """A coiled hive, like a straw bee skep, on a stand, its doorway facing the camera."""
    rows = 7
    for row in range(rows):
        z = 0.1 + row * 0.2
        radius = 0.56 * math.cos(row / rows * math.pi / 2.1)
        count = max(10, int(60 * radius))
        for k in range(count):
            a = 2 * math.pi * k / count
            d.ball((radius * math.cos(a), radius * math.sin(a), z), 0.12, stiffness=4.0)
    d.ball((0, 0, 0.1 + rows * 0.2 - 0.08), 0.16)
    d.ellipsoid((0, 0, 0.0), 0.47, (1.0, 1.0, 2.5))
    # A low woven stand widens the footprint without adding much bulk.
    d.ellipsoid((0, 0, -0.02), 1.2, (1.0, 1.0, 0.06), stiffness=4.0)
    camera = math.atan2(CAMERA[1], CAMERA[0])
    door = (0.58 * math.cos(camera), 0.58 * math.sin(camera), 0.3)
    d.ellipsoid(door, 0.3, (0.75, 0.75, 1.15), stiffness=8.0, negative=True)


def bloom(d):
    """Petals cupped open around a central bud."""
    d.ellipsoid((0, 0, 0.0), 0.6, (1.0, 1.0, 0.35))
    d.ellipsoid((0, 0, 0.45), 0.36, (1.0, 1.0, 1.4))
    count = 6
    for k in range(count):
        a = 2 * math.pi * k / count + d.jitter(0.08)
        out = 0.55 + d.jitter(0.05)
        centre = (out * math.cos(a), out * math.sin(a), 0.42)
        axis = (0.75 * math.cos(a), 0.75 * math.sin(a), 1.0)
        d.ellipsoid(centre, 0.42, (0.55, 0.22, 1.25), axis=axis)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        d.chain((0.12 * math.cos(a), 0.12 * math.sin(a), 0.75), (0.3 * math.cos(a), 0.3 * math.sin(a), 1), 0.45, 0.07, 0.04)
        d.ball((0.12 * math.cos(a) + 0.13 * math.cos(a), 0.12 * math.sin(a) + 0.13 * math.sin(a), 1.2), 0.09)


DESIGNS = {'crown': (crown, 11), 'clutch': (clutch, 12), 'toadstool': (toadstool, 13),
           'coral': (coral, 14), 'skep': (skep, 15), 'bloom': (bloom, 16)}


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    for block in (bpy.data.meshes, bpy.data.metaballs):
        for item in list(block):
            block.remove(item)


def surface(design, resolution):
    """Polygonize the metaballs, keep only what stands above the ground."""
    data = bpy.data.metaballs.new('swarm')
    data.resolution = data.render_resolution = resolution
    data.threshold = THRESHOLD
    for kind, co, radius, stiffness, size, axis, negative in design.elements:
        element = data.elements.new(type=kind)
        # Designs give the radius of the surface; an isolated element's surface
        # lies where its falloff reaches the threshold, inside its radius.
        reach = math.sqrt(1 - math.sqrt(THRESHOLD / stiffness))
        element.co, element.radius, element.stiffness = co, radius / reach, stiffness
        element.use_negative = negative
        if size:
            element.size_x, element.size_y, element.size_z = size
            element.rotation = Vector(axis).normalized().to_track_quat('Z', 'Y')
    meta = bpy.data.objects.new('swarm-meta', data)
    bpy.context.scene.collection.objects.link(meta)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(meta.evaluated_get(depsgraph))
    bpy.data.objects.remove(meta)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    geometry = bm.verts[:] + bm.edges[:] + bm.faces[:]
    # Faces below ground are never drawn; leave the base open on the ground.
    bmesh.ops.bisect_plane(bm, geom=geometry, plane_co=(0, 0, 0), plane_no=(0, 0, 1), clear_inner=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-6)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new('swarm', mesh)
    bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    return obj


def decimate(obj):
    obj.data.calc_loop_triangles()
    modifier = obj.modifiers.new('SkinSurfaceBudget', 'DECIMATE')
    modifier.ratio = min(1.0, TRIANGLES / len(obj.data.loop_triangles))
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')


def camera_fit():
    """One projection for every variant, fitted to the shared design volume."""
    rotation = np.array(Vector(CAMERA).to_track_quat('Z', 'Y').to_matrix()).T
    ring = np.linspace(0, 2 * math.pi, 256, endpoint=False)
    volume = np.array([(math.cos(a), math.sin(a), z) for a in ring for z in (0.0, HEIGHT)])
    view = volume @ rotation.T
    low, high = view.min(axis=0), view.max(axis=0)
    scale = CANVAS_FIT / (high[:2] - low[:2]).max()
    return rotation, (low + high) / 2, scale, DEPTH_RANGE / (high[2] - low[2])


def project(obj):
    rotation, centre, scale, depth = camera_fit()
    mesh = obj.data
    mesh.calc_loop_triangles()
    mesh.calc_normals()
    positions = np.array([v.co[:] for v in mesh.vertices])
    view = positions @ rotation.T - centre
    view[:, :2] *= scale
    view[:, 2] *= -depth
    normals = np.array([v.normal[:] for v in mesh.vertices]) @ rotation.T
    if np.abs(view[:, :2]).max() > 0.97:
        raise ValueError(f'{obj.name} leaves the swarm canvas: {np.abs(view[:, :2]).max():.3f}')
    return view, normals


def weight(obj):
    """Screen coverage in sprite pixels, and enclosed volume in design units."""
    mesh = obj.data
    mesh.calc_loop_triangles()
    triangles = np.array([t.vertices[:] for t in mesh.loop_triangles])
    positions = np.array([v.co[:] for v in mesh.vertices])
    rotation, centre, scale, depth = camera_fit()
    view = positions @ rotation.T - centre
    size = SPRITE * SUPERSAMPLE
    screen = np.stack([(view[:, 0] * scale + 1) / 2 * size, (1 - view[:, 1] * scale) / 2 * size], axis=1)
    coverage = float((rasterize(screen, -view[:, 2], triangles, size) >= 0).sum()) / SUPERSAMPLE ** 2
    # Divergence theorem with F = (0, 0, z): the open base lies on z = 0 and adds nothing.
    a, b, c = (positions[triangles[:, i]] for i in range(3))
    normal_z = np.cross(b - a, c - a)[:, 2] / 2
    volume = float(((a[:, 2] + b[:, 2] + c[:, 2]) / 3 * normal_z).sum())
    return coverage, volume


def fit(obj):
    """Scale uniformly about the ground centre to the shared screen coverage."""
    factor = math.sqrt(COVERAGE / weight(obj)[0])
    for vertex in obj.data.vertices:
        vertex.co *= factor
    coverage, volume = weight(obj)
    return {'coveragePx': round(coverage, 1), 'volume': round(volume, 4), 'fitScale': round(factor, 4)}


def unwrap(view):
    """Paint layout projected along the game camera.

    The swarm has one static pose and is only seen and painted from this
    camera, so a projection leaves no seams on the visible surface and gives
    every visible pixel the same texel density. The silhouette's bounds fill
    the texture with square texels and a 2 texel border; hidden surfaces share
    texels with the surfaces in front of them.
    """
    low, high = view[:, :2].min(axis=0), view[:, :2].max(axis=0)
    border = 2 / 256
    return border + (view[:, :2] - low) / (high - low).max() * (1 - 2 * border)


def write(obj, path):
    view, normals = project(obj)
    uv = unwrap(view)
    mesh = obj.data
    triangles = np.array([t.vertices[:] for t in mesh.loop_triangles], dtype='<u4')
    if len(view) > MAX_VERTICES or triangles.size > MAX_INDICES:
        raise ValueError(f'{path.name} exceeds runtime geometry budget: {len(view)} vertices, {len(triangles)} triangles')
    # Image rows run top to bottom; view y runs up.
    texture = np.stack([uv[:, 0], 1 - uv[:, 1]], axis=1)
    with path.open('wb') as stream:
        stream.write(struct.pack('<4sIIII', b'GSK1', len(view), triangles.size, 1, 96))
        stream.write(texture.astype('<f4').tobytes())
        stream.write(triangles.tobytes())
        stream.write(np.hstack([view, normals]).astype('<f4').tobytes())
    return len(view), len(triangles)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(name, output, resolution):
    build, seed = DESIGNS[name]
    clear_scene()
    design = Design(seed)
    build(design)
    obj = surface(design, resolution)
    obj.name = name
    decimate(obj)
    fitted = fit(obj)
    path = output / f'swarm-{name}.gsk'
    vertices, triangles = write(obj, path)
    here = Path(__file__).resolve().parent
    return path.name, {
        'sha256': sha256(path),
        'source': 'tools/skins/generate_swarms.py',
        'sourceSha256': sha256(here / 'generate_swarms.py'),
        'dependencies': {'tools/skins/swarm_metrics.py': sha256(here / 'swarm_metrics.py')},
        'design': name, 'seed': seed, 'metaballResolution': resolution,
        **fitted,
        'vertices': vertices, 'triangles': triangles,
    }


def install(output, records):
    """Record generated meshes in the colony-v1 manifest beside them."""
    path = output / 'manifest.json'
    manifest = json.loads(path.read_text())
    if manifest.get('layout') != 'colony-v1':
        raise ValueError(f'{path} is not a colony-v1 manifest')
    manifest['meshes'].update(records)
    path.write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path,
                        help='directory for swarm-<design>.gsk; a colony-v1 manifest.json there is updated')
    parser.add_argument('--designs', default=','.join(DESIGNS))
    parser.add_argument('--resolution', type=float, default=0.03)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError('Use Blender 3.6.23')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = dict(generate(name, output, args.resolution) for name in args.designs.split(','))
    if (output / 'manifest.json').exists():
        install(output, records)
    print(json.dumps(records, indent=2), flush=True)
