# SPDX-License-Identifier: GPL-3.0-or-later
"""Warrior/explorer authoring contracts; run with pinned Blender 3.6.23."""

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
from author_unit_rigs import ROOT, CLIPS, author, rest_surface
from export_rig import _matrix


def intersections(positions, triangles):
    tree = BVHTree.FromPolygons(
        positions.tolist(), triangles.tolist(), all_triangles=True
    )
    return {
        (min(a, b), max(a, b))
        for a, b in tree.overlap(tree)
        if a != b and not set(triangles[a]) & set(triangles[b])
    }


class UnitRigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "artifacts").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(
            prefix="unit-rigs-", dir=ROOT / "artifacts"
        )
        cls.folder = Path(cls.temporary.name)
        for model in CLIPS:
            author(model, cls.folder / model)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def open(self, model):
        p, _, _, faces, *_ = rest_surface(model)
        _, self.first, inverse = np.unique(
            p, axis=0, return_index=True, return_inverse=True
        )
        self.faces = inverse[faces]
        self.original_to_welded = inverse
        bpy.ops.wm.open_mainfile(
            filepath=str(self.folder / model / (model + ".blend")), use_scripts=False
        )
        self.rig = bpy.data.objects[model.title() + "Rig"]
        self.obj = bpy.data.objects[model.title() + "RestSurface"]

    def posed(self):
        evaluated = self.obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = evaluated.to_mesh()
        try:
            return np.array([v.co[:] for v in mesh.vertices])[self.first]
        finally:
            evaluated.to_mesh_clear()

    def test_rest_weights_normals_and_welded_seams(self):
        for model in CLIPS:
            p, n, uv, faces, influences, bones, *_ = rest_surface(model)
            np.testing.assert_allclose(np.linalg.norm(n, axis=1), 1, atol=1e-6)
            _, first, inverse = np.unique(
                p, axis=0, return_index=True, return_inverse=True
            )
            np.testing.assert_allclose(n, n[first][inverse], atol=1e-6)
            self.assertFalse(intersections(p[first], inverse[faces]), model)
            for joints, weights in influences:
                self.assertAlmostEqual(sum(weights), 1)
                self.assertTrue(all(w >= 0 for w in weights))
                self.assertTrue(all(0 <= j < len(bones) for j in joints))

    def test_actions_have_no_intersections_and_close(self):
        for model, clips in CLIPS.items():
            self.open(model)
            for clip in clips:
                self.rig.animation_data.action = bpy.data.actions[clip.title()]
                poses = []
                for frame in range(1, 66):
                    bpy.context.scene.frame_set(frame)
                    p = self.posed()
                    self.assertTrue(np.isfinite(p).all())
                    self.assertFalse(intersections(p, self.faces), (model, clip, frame))
                    poses.append(p)
                np.testing.assert_allclose(poses[0], poses[-1], atol=1e-5)
                speeds = np.linalg.norm(np.diff(poses, axis=0), axis=2).max(axis=1)
                self.assertLessEqual(speeds[-1], max(speeds[:-1]) * 1.1)

    def test_swim_follows_the_source_stroke_and_retracts(self):
        from author_unit_rigs import reference_motion

        _, motion = reference_motion("warrior", "swim")
        self.open("warrior")
        self.rig.animation_data.action = bpy.data.actions["Swim"]
        names = ["arm.lower.R", "arm.lower.L", "leg.lower.R", "leg.lower.L"]
        angles, reaches = [], []
        for frame, parts in enumerate(motion, 1):
            bpy.context.scene.frame_set(frame)
            origin = self.rig.pose.bones["body"].head
            for name, part in zip(names, [4, 3, 1, 2]):
                actual = self.rig.pose.bones[name].head - origin
                expected = parts[part].translation - parts[0].translation
                if expected.length > 6.5:
                    angles.append(actual.angle(expected))
                reaches.append(actual.length)
        # Follow the source on the outward stroke, then gather much closer
        # than the old six-unit clearance floor during recovery. The compact
        # directions separate the caps rather than duplicating metaball unions.
        self.assertLess(np.degrees(np.mean(angles)), 2)
        self.assertGreaterEqual(min(reaches), 3.5 - 1e-5)
        self.assertLess(min(reaches), 3.6)
        self.assertGreater(max(reaches) - min(reaches), 3.4)
        # Inspect interpolated poses as well as the authored integer samples.
        for sample in range(256):
            bpy.context.scene.frame_set(1 + sample // 4, subframe=(sample % 4) / 4)
            self.assertFalse(intersections(self.posed(), self.faces), sample)

    def test_warrior_terminals_keep_their_shape_through_all_actions(self):
        from limb_surface import LimbSurface
        import json

        definition = json.loads(
            (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
        )["warrior"]
        surface = LimbSurface(definition)
        caps = [
            [
                i
                for i, descriptor in enumerate(surface.vertices)
                if descriptor[0] in ("cap", "tip") and descriptor[1] == branch
            ]
            for branch in range(4)
        ]
        self.open("warrior")
        rest = np.array([v.co[:] for v in self.obj.data.vertices])
        reference = [np.linalg.norm(rest[cap] - rest[cap[0]], axis=1) for cap in caps]
        for clip in CLIPS["warrior"]:
            self.rig.animation_data.action = bpy.data.actions[clip.title()]
            for frame in range(1, 65):
                bpy.context.scene.frame_set(frame)
                pose = self.posed()[self.original_to_welded]
                for cap, expected in zip(caps, reference):
                    np.testing.assert_allclose(
                        np.linalg.norm(pose[cap] - pose[cap[0]], axis=1),
                        expected,
                        atol=1e-5,
                    )

    def test_warrior_clips_share_identical_rest_mesh_weights_and_bones(self):
        data = [
            (self.folder / "warrior" / f"warrior-{clip}.gsr").read_bytes()
            for clip in CLIPS["warrior"]
        ]
        _, v, n, b, *_ = struct.unpack_from("<4s6I", data[0])
        end = 28 + v * 64 + n * 4 + b * 68
        for clip in data[1:]:
            self.assertEqual(data[0][:end], clip[:end])

    def test_warrior_shafts_stay_straight_and_keep_their_cross_sections(self):
        import json
        from limb_surface import LimbSurface

        surface = LimbSurface(
            json.loads(
                (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
            )["warrior"]
        )
        rings = np.array(
            [
                [
                    [
                        i
                        for i, d in enumerate(surface.vertices)
                        if d[0] == "ring" and d[1] == branch and d[2] == ring / 9
                    ]
                    for ring in range(1, 10)
                ]
                for branch in range(4)
            ]
        )
        self.open("warrior")
        rest = np.array([v.co[:] for v in self.obj.data.vertices])[rings]
        reference = np.linalg.norm(rest - rest[:, :, :1], axis=-1)
        for clip in CLIPS["warrior"]:
            self.rig.animation_data.action = bpy.data.actions[clip.title()]
            for frame in range(1, 65):
                bpy.context.scene.frame_set(frame)
                pose = self.posed()[self.original_to_welded][rings]
                np.testing.assert_allclose(
                    np.linalg.norm(pose - pose[:, :, :1], axis=-1),
                    reference,
                    atol=1e-5,
                )
                centers = pose.mean(axis=2)
                axis = centers[:, -1] - centers[:, 0]
                axis /= np.linalg.norm(axis, axis=1)[:, None]
                deviation = np.linalg.norm(
                    np.cross(centers - centers[:, :1], axis[:, None]), axis=-1
                )
                self.assertLess(deviation.max(), 1e-5, (clip, frame))

    def test_warrior_girth_does_not_collapse_into_thin_stalks(self):
        import json
        from author_unit_rigs import baked
        from limb_surface import LimbSurface

        surface = LimbSurface(
            json.loads(
                (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
            )["warrior"]
        )
        rings = [
            [
                [
                    i
                    for i, d in enumerate(surface.vertices)
                    if d[0] == "ring" and d[1] == branch and d[2] == ring / 9
                ]
                for ring in range(1, 10)
            ]
            for branch in range(4)
        ]

        def girth(points):
            return np.linalg.norm(points - np.roll(points, 1, axis=-2), axis=-1).sum(
                axis=-1
            )

        original = baked("warrior", "walk")[3]
        reference = girth(original[np.array(rings)[:, -1]])
        self.open("warrior")
        for clip in CLIPS["warrior"]:
            self.rig.animation_data.action = bpy.data.actions[clip.title()]
            for frame in range(1, 65):
                bpy.context.scene.frame_set(frame)
                pose = self.posed()[self.original_to_welded]
                circumferences = girth(pose[np.array(rings)])
                # Keep the end volumes near the original size, and prevent a
                # symmetric but scrawny connector regression in any action.
                np.testing.assert_allclose(circumferences[:, -1], reference, rtol=0.05)
                self.assertTrue(
                    np.all(circumferences >= reference[:, None] * 0.6),
                    (clip, frame, circumferences.min()),
                )

    def test_warrior_is_symmetric_left_right_and_top_bottom(self):
        import json
        from limb_surface import LimbSurface

        definition = json.loads(
            (ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json").read_text()
        )["warrior"]
        surface = LimbSurface(definition)
        p, normals, _, _, influences, bones, *_ = rest_surface("warrior")
        origin = np.array(bones[0][2])
        dense = np.zeros((len(p), len(bones)))
        for row, (joints, weights) in enumerate(influences):
            for joint, weight in zip(joints, weights):
                dense[row, joint] += weight
        lookup = {tuple(np.round(v, 8)): i for i, v in enumerate(surface.virtual)}
        for axis, bone_map in [
            (1, [0, 3, 4, 1, 2, 7, 8, 5, 6]),
            (2, [0, 5, 6, 7, 8, 1, 2, 3, 4]),
        ]:
            reflection = np.ones(3)
            reflection[axis] = -1
            pairs = [
                lookup[tuple(np.round(np.array(v) * reflection, 8))]
                for v in surface.virtual
            ]
            # Check every vertex, including the junctions and torso, rather
            # than just equal terminal radii or a symmetric silhouette.
            np.testing.assert_allclose(
                p[pairs] - origin, (p - origin) * reflection, atol=1e-6
            )
            np.testing.assert_allclose(normals[pairs], normals * reflection, atol=1e-6)
            np.testing.assert_allclose(dense[pairs][:, bone_map], dense, atol=1e-8)
            for i in range(1, len(bones)):
                for endpoint in [2, 3]:
                    np.testing.assert_allclose(
                        np.array(bones[bone_map[i]][endpoint]) - origin,
                        (np.array(bones[i][endpoint]) - origin) * reflection,
                        atol=1e-6,
                    )

    def test_action_roundtrip_and_overwrite_guard(self):
        for model in CLIPS:
            clip = CLIPS[model][0]
            source = self.folder / model / (model + ".blend")
            target = self.folder / (model + "-roundtrip")
            author(model, target, source, clip.title(), clip)
            a = (self.folder / model / f"{model}-{clip}.gsr").read_bytes()
            b = (target / f"{model}-{clip}.gsr").read_bytes()
            _, v, n, bones, *_ = struct.unpack_from("<4s6I", a)
            start = 28 + v * 64 + n * 4 + bones * 68 + 128 + 2048
            self.assertEqual(a[:start], b[:start])
            for x, y in zip(
                np.frombuffer(a, "<f4", offset=start).reshape(-1, 8),
                np.frombuffer(b, "<f4", offset=start).reshape(-1, 8),
            ):
                np.testing.assert_allclose(_matrix(x), _matrix(y), atol=2e-5)
            with self.assertRaisesRegex(ValueError, "separate"):
                author(model, self.folder / model, source, clip.title(), clip)

    def test_warrior_action_import_rejects_bending_or_sideways_distal_handles(self):
        for channel in ["rotation_quaternion", "location"]:
            self.open("warrior")
            bpy.context.scene.frame_set(1)
            distal = self.rig.pose.bones["arm.lower.R"]
            if channel == "rotation_quaternion":
                distal.rotation_quaternion = Quaternion((1, 0, 0), 0.2)
            else:
                distal.location.x += 0.2
            distal.keyframe_insert(channel, frame=1)
            source = self.folder / "bent-warrior.blend"
            bpy.ops.wm.save_as_mainfile(filepath=str(source))
            with self.assertRaisesRegex(ValueError, "straight shaft constraint"):
                author("warrior", self.folder / "bent-output", source, "Walk", "walk")

    def test_independent_bones_can_pose_without_reference_motion(self):
        for model, names in [
            ("warrior", ["arm.upper.R", "arm.lower.L", "leg.upper.R", "leg.lower.L"]),
            ("explorer", ["head", "wing.R", "wing.L"]),
        ]:
            self.open(model)
            self.rig.animation_data.action = None
            for name in names:
                for axis in [(1, 0, 0), (0, 0, 1)]:
                    for angle in [-0.5, 0.5]:
                        for bone in self.rig.pose.bones:
                            bone.matrix_basis = Matrix.Identity(4)
                        self.rig.pose.bones[name].rotation_quaternion = Quaternion(
                            axis, angle
                        )
                        bpy.context.view_layer.update()
                        self.assertFalse(
                            intersections(self.posed(), self.faces),
                            (model, name, axis, angle),
                        )


if (
    not unittest.TextTestRunner(verbosity=2)
    .run(unittest.defaultTestLoader.loadTestsFromTestCase(UnitRigTest))
    .wasSuccessful()
):
    raise RuntimeError("Unit authoring contract failed")
