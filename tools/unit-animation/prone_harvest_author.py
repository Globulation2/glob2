# SPDX-License-Identifier: GPL-3.0-or-later
"""Author the retained 64-phase harvest motion with Blender 3.6.23.
Staging only: use prone_harvest.py to render, validate and install.
"""
import sys, json, math
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix
import argparse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
OUT = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :]).output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
assert bpy.app.version[:3] == (3, 6, 23), "Use Blender 3.6.23"

sys.path.insert(0, str(ROOT / "tools/skins"))
import export_units as e
from limb_surface import LimbSurface, Tracker
from chart import chart

_, s = e.open_source("worker", "walk")
parts = e.components(s)
surface = LimbSurface(
    json.loads(
        (ROOT / "content-src/gfx/authored/skins/limb-surfaces.json").read_text()
    )["worker"]
)
tracker = Tracker(
    surface,
    e.rest_components(s, parts),
    np.array([p.data.elements[0].radius for p in parts]),
    np.array([p.data.elements[0].stiffness for p in parts]),
    parts[0].data.threshold,
    "walk",
)
uv = chart(surface, tracker.positions)
_, s = e.open_source("worker", "swim")
parts = e.components(s)
s.frame_set(1)
original = e.influence_matrices(parts)
cam = np.linalg.inv(np.array(s.camera.matrix_world))
scale = -cam[2, 3] * 32 / s.camera.data.lens
from prone_harvest_motion import pose

frames = []
verts = []
normals = []
for i in range(65):
    m = pose(i / 64, original)
    p = tracker.evaluate(m, "swim", i % 64)
    frames.append(m)
    verts.append(p)
    normals.append(surface.normals(p))
    print("sample", i, flush=True)
assert np.max(np.abs(verts[0] - verts[-1])) < 1e-8
np.savez_compressed(
    OUT / "motion.npz",
    components=frames,
    vertices=verts,
    normals=normals,
    triangles=surface.triangles,
    uv=uv,
    camera=cam,
    scale=scale,
)
# Retain an editable modern scene with actual component animation.
for p in parts:
    p.parent = None
    p.animation_data_clear()
    p.rotation_euler = (0, 0, 0)
    p.scale = np.linalg.norm(original[parts.index(p), :3, :3], axis=0).tolist()
for o in s.objects:
    if o.type == "META":
        continue
    if o.type in {"ARMATURE", "EMPTY"}:
        o.hide_render = True
for i, m in enumerate(frames):
    for j, p in enumerate(parts):
        p.location = m[j, :3, 3]
        p.scale = np.linalg.norm(m[j, :3, :3], axis=0).tolist()
        p.keyframe_insert(data_path="location", frame=i + 1)
        p.keyframe_insert(data_path="scale", frame=i + 1)
for p in parts:
    for f in p.animation_data.action.fcurves:
        for k in f.keyframe_points:
            k.interpolation = "LINEAR"
s.frame_start = 1
s.frame_end = 64
s.render.fps = 100
s.render.fps_base = 3
s.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "worker-prone-harvest.blend"))
(OUT / "motion.json").write_text(
    json.dumps(
        {
            "version": 1,
            "samples": 64,
            "previewSeconds": 1.92,
            "source": "glob-worker-swim.blend",
            "meshTopology": "worker-v2",
            "loopPositionError": float(np.max(np.abs(verts[0] - verts[-1]))),
            "notes": "Revision 4: continuous rounded cubic stroke, relaxed recovery, compliant delayed elbows, 4.5% hand overlap, body yield; 64 review poses over 1.92 seconds. Cosmetic loop timing is independent of harvesting orders.",
        },
        indent=2,
    )
)

import hashlib

sources = [
    "content-src/gfx/originals/units/glob-worker-swim.blend",
    "content-src/gfx/originals/units/glob-worker-walk.blend",
    "content-src/gfx/authored/skins/limb-surfaces.json",
    "content-src/gfx/authored/skins/limb-paint-charts.json",
    "tools/unit-animation/prone_harvest_motion.py",
    "tools/unit-animation/prone_harvest_author.py",
    "tools/skins/export_units.py",
    "tools/skins/chart.py",
    "tools/skins/limb_surface.py",
    "tools/skins/connected_surface.py",
]
(OUT / "author-receipt.json").write_text(
    json.dumps(
        dict(
            sources={
                p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources
            },
            motion=hashlib.sha256((OUT / "motion.npz").read_bytes()).hexdigest(),
        ),
        indent=2,
    )
    + "\n"
)
