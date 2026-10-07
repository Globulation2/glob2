# SPDX-License-Identifier: GPL-3.0-or-later
"""Authored worker acceptance checks; run in pinned Blender like test_rig_export.py."""

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Matrix, Quaternion
from mathutils.bvhtree import BVHTree
from author_worker_rig import author, ROOT
from worker_surface import build_surface, skin_weights
from limb_surface import LimbSurface


def intersections(positions, triangles):
    tree = BVHTree.FromPolygons(
        positions.tolist(), triangles.tolist(), all_triangles=True
    )
    return {
        (min(a, b), max(a, b))
        for a, b in tree.overlap(tree)
        if a != b and not set(triangles[a]) & set(triangles[b])
    }


class WorkerRigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "artifacts").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(
            prefix="worker-rig-", dir=ROOT / "artifacts"
        )
        cls.folder = Path(cls.temporary.name)
        author(cls.folder)
        cls.definition = json.loads(
            (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
        )["worker"]
        cls.surface = LimbSurface(cls.definition)
        cls.positions, cls.normals, cls.anchors = build_surface(cls.definition)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        bpy.ops.wm.open_mainfile(
            filepath=str(self.folder / "worker-walk.blend"), use_scripts=False
        )
        self.rig = bpy.data.objects["WorkerRig"]
        self.obj = bpy.data.objects["WorkerRestSurface"]

    def posed(self):
        evaluated = self.obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.to_mesh()
        try:
            return np.array([v.co[:] for v in mesh.vertices])
        finally:
            evaluated.to_mesh_clear()

    def test_rest_surface_is_closed_outward_symmetric_and_keeps_paint(self):
        triangles = self.surface.triangles
        edges = {}
        for face in triangles:
            for a, b in zip(face, np.roll(face, -1)):
                edges.setdefault(tuple(sorted((a, b))), []).append((a, b))
        self.assertTrue(
            all(len(pair) == 2 and pair[0] == pair[1][::-1] for pair in edges.values())
        )
        self.assertFalse(intersections(self.positions, triangles))
        face = np.cross(
            self.positions[triangles[:, 1]] - self.positions[triangles[:, 0]],
            self.positions[triangles[:, 2]] - self.positions[triangles[:, 0]],
        )
        self.assertTrue(np.all(np.linalg.norm(face, axis=1) > 1e-7))
        self.assertTrue(
            np.all((face * self.normals[triangles].mean(axis=1)).sum(axis=1) > 0)
        )
        for name, axis in [("frontBack", 0), ("topBottom", 2)]:
            reflected = self.positions.copy()
            reflected[:, axis] *= -1
            pairs = self.surface.contract()["reflections"][name]
            np.testing.assert_allclose(reflected, self.positions[pairs], atol=1e-12)
        data = (ROOT / "data/skins/colony-v1/worker-walk.gsk").read_bytes()
        uv = np.frombuffer(data, "<f4", len(self.positions) * 2, 20).reshape(-1, 2)
        np.testing.assert_array_equal(uv, self.surface.uv.astype("<f4"))

    def test_torso_is_tall_and_sockets_are_separated(self):
        from worker_surface import BODY_HALF_EXTENSION

        body = self.positions[
            [i for i, d in enumerate(self.surface.vertices) if d[0] == "body"]
        ]
        size = np.ptp(body, axis=0)
        self.assertGreater(size[2] / size[1], 1.4)
        for name in ("arm.attach.R", "arm.attach.L"):
            self.assertAlmostEqual(
                self.rig.data.bones[name].head_local.z, BODY_HALF_EXTENSION, places=6
            )
        for name in ("leg.attach.R", "leg.attach.L"):
            self.assertAlmostEqual(
                self.rig.data.bones[name].head_local.z, -BODY_HALF_EXTENSION, places=6
            )

    def test_waist_shape_preserves_smooth_chest_and_limb_geometry(self):
        plain, _, _ = build_surface(self.definition, sculpt=False)
        body = np.array([d[0] == "body" for d in self.surface.vertices])
        limbs = np.array(
            [d[0] in ("ring", "cap", "tip") for d in self.surface.vertices]
        )
        np.testing.assert_array_equal(self.positions[limbs], plain[limbs])
        depth = np.abs(self.positions[:, 0]) - np.abs(plain[:, 0])
        self.assertLessEqual(depth[body].max(), 1e-12)
        width = np.abs(self.positions[:, 1]) - np.abs(plain[:, 1])
        self.assertLessEqual(width[body].max(), 1e-12)
        middle = body & (np.abs(plain[:, 2]) < 0.1)
        # A waist can narrow in depth, width, or both. Preserve the shape
        # contract without fixing the proportions to one authoring choice.
        self.assertLess(
            (
                np.ptp(self.positions[middle, :2], axis=0)
                / np.ptp(plain[middle, :2], axis=0)
            ).min(),
            0.94,
        )

    def test_torso_curl_bends_both_ends_without_rotating_the_root(self):
        import author_worker_rig
        from unittest.mock import patch

        def capture():
            self.rig = bpy.data.objects["WorkerRig"]
            self.obj = bpy.data.objects["WorkerRestSurface"]
            bpy.context.scene.frame_set(17)
            root = self.rig.pose.bones["body"].matrix.copy()
            p = self.posed()
            local = (
                np.column_stack((p, np.ones(len(p)))) @ np.array(root.inverted()).T
            )[:, :3]
            return np.array(root), local

        bent_root, bent = capture()
        with patch.object(author_worker_rig, "torso_curl_angle", return_value=0):
            author(self.folder / "uncurled")
        plain_root, plain = capture()
        np.testing.assert_allclose(bent_root, plain_root, atol=1e-6)
        body = np.array([d[0] == "body" for d in self.surface.vertices])
        for sign in [-1, 1]:
            end = body & (self.positions[:, 2] * sign > 2.5)
            self.assertLess((bent[end] - plain[end])[:, 0].mean(), -0.1)
        waist = body & (np.abs(self.positions[:, 2]) < 0.1)
        self.assertLess(abs((bent[waist] - plain[waist])[:, 0].mean()), 0.03)

    def test_weights_preserve_symmetry_and_do_not_truncate_support(self):
        weights = skin_weights(self.positions, self.anchors, self.definition["paths"])
        dense = np.zeros((len(weights), 13))
        for v, (j, w) in enumerate(weights):
            for joint, weight in zip(j, w):
                dense[v, joint] += weight
        np.testing.assert_allclose(dense.sum(axis=1), 1, atol=1e-12)
        self.assertTrue(np.all(dense >= 0))
        np.testing.assert_allclose(
            dense,
            dense[self.surface.contract()["reflections"]["frontBack"]],
            atol=1e-12,
        )
        self.assertTrue(np.all((dense > 0).sum(axis=1) <= 4))
        # A terminal must never receive a neighbouring limb's motion.
        for v, descriptor in enumerate(self.surface.vertices):
            if descriptor[0] == "tip":
                self.assertAlmostEqual(dense[v, 3 + descriptor[1] * 3], 1)

    def test_walk_has_no_self_intersections_and_closes_without_a_pop(self):
        poses = []
        for frame in range(1, 66):
            bpy.context.scene.frame_set(frame)
            p = self.posed()
            self.assertTrue(np.isfinite(p).all())
            self.assertFalse(intersections(p, self.surface.triangles), f"frame {frame}")
            poses.append(p)
        np.testing.assert_allclose(poses[0], poses[-1], atol=1e-5)
        speeds = np.linalg.norm(np.diff(np.array(poses), axis=0), axis=2).max(axis=1)
        self.assertLessEqual(speeds[-1], max(speeds[:-1]) * 1.1)
        self.assertEqual(bpy.context.scene.render.fps, 32)

    def test_shafts_stay_straight_and_caps_keep_their_shape(self):
        rings = []
        caps = []
        for branch in range(4):
            times = sorted(
                {
                    d[2]
                    for d in self.surface.vertices
                    if d[0] == "ring" and d[1] == branch
                }
            )
            rings.append(
                [
                    [
                        i
                        for i, d in enumerate(self.surface.vertices)
                        if d[0] == "ring" and d[1] == branch and d[2] == t
                    ]
                    for t in times
                ]
            )
            caps.append(
                [
                    i
                    for i, d in enumerate(self.surface.vertices)
                    if d[0] in ("cap", "tip") and d[1] == branch
                ]
            )
        rings, caps = np.array(rings), np.array(caps)

        def distances(p):
            return np.linalg.norm(p - p[..., :1, :], axis=-1)

        ring_reference = distances(self.positions[rings])
        cap_reference = distances(self.positions[caps])
        for sample in range(256):
            bpy.context.scene.frame_set(1 + sample // 4, subframe=(sample % 4) / 4)
            pose = self.posed()
            np.testing.assert_allclose(
                distances(pose[rings]), ring_reference, atol=1e-5
            )
            np.testing.assert_allclose(distances(pose[caps]), cap_reference, atol=1e-5)
            centers = pose[rings].mean(axis=2)
            axis = centers[:, -1] - centers[:, 0]
            axis /= np.linalg.norm(axis, axis=1)[:, None]
            deviation = np.linalg.norm(
                np.cross(centers - centers[:, :1], axis[:, None]), axis=-1
            )
            self.assertLess(deviation.max(), 1e-5, sample)
            self.assertFalse(intersections(pose, self.surface.triangles), sample)

    def test_action_import_rejects_independent_joint_bends_and_sideways_offsets(self):
        source = self.folder / "invalid.blend"
        for channel, value in [
            ("rotation_quaternion", Quaternion((1, 0, 0), 0.2)),
            ("location", (0.2, 0, 0)),
        ]:
            bpy.ops.wm.open_mainfile(
                filepath=str(self.folder / "worker-walk.blend"), use_scripts=False
            )
            rig = bpy.data.objects["WorkerRig"]
            bone = rig.pose.bones["arm.upper.R"]
            setattr(bone, channel, value)
            bone.keyframe_insert(channel, frame=1)
            bpy.ops.wm.save_as_mainfile(filepath=str(source))
            with self.assertRaisesRegex(ValueError, "straight shaft constraint"):
                author(self.folder / "invalid-export", source, "Walk")

    def test_edited_action_exports_against_the_same_base(self):
        original = (self.folder / "worker-walk.gsr").read_bytes()
        source = self.folder / "worker-walk.blend"
        target = self.folder / "roundtrip"
        author(target, source, "Walk")
        exported = (target / "worker-walk.gsr").read_bytes()
        _, vertices, indices, bones, _, _, _ = struct.unpack_from("<4s6I", original)
        tracks_at = 28 + vertices * 64 + indices * 4 + bones * 68 + 128 + 2048
        self.assertEqual(original[:tracks_at], exported[:tracks_at])
        from export_rig import _matrix

        original_tracks = np.frombuffer(original, "<f4", offset=tracks_at).reshape(
            -1, 8
        )
        exported_tracks = np.frombuffer(exported, "<f4", offset=tracks_at).reshape(
            -1, 8
        )
        for a, b in zip(original_tracks, exported_tracks):
            np.testing.assert_allclose(_matrix(a), _matrix(b), atol=2e-5)
        with self.assertRaisesRegex(ValueError, "different output"):
            author(self.folder, source, "Walk")

    def test_whole_lobe_rotations_keep_the_surface_intact(self):
        self.rig.animation_data.action = None
        for name in ("arm.attach.R", "arm.attach.L", "leg.attach.R", "leg.attach.L"):
            for axis in [(1, 0, 0), (0, 0, 1)]:
                for angle in [-0.5, 0.5]:
                    for bone in self.rig.pose.bones:
                        bone.matrix_basis = Matrix.Identity(4)
                    self.rig.pose.bones[name].rotation_quaternion = Quaternion(
                        axis, angle
                    )
                    bpy.context.view_layer.update()
                    p = self.posed()
                    self.assertFalse(
                        intersections(p, self.surface.triangles), (name, axis, angle)
                    )


if (
    not unittest.TextTestRunner(verbosity=2)
    .run(unittest.defaultTestLoader.loadTestsFromTestCase(WorkerRigTest))
    .wasSuccessful()
):
    raise RuntimeError("Worker authoring contract failed")
