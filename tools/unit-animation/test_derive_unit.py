# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-side checks for derive_unit.py and the wizard spec (no Blender needed).

Run with: python3 tools/unit-animation/test_derive_unit.py
"""
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_unit as D  # noqa: E402
import wizard_unit as W  # noqa: E402


def rotate(q, v):
    """Rotate vector v by unit quaternion q = (w, x, y, z)."""
    w, x, y, z = q
    # v' = v + 2w(u x v) + 2u x (u x v), with u = (x, y, z)
    u = (x, y, z)
    c1 = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    c2 = (u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0])
    return tuple(v[i] + 2 * w * c1[i] + 2 * c2[i] for i in range(3))


class MathTest(unittest.TestCase):
    def test_arc_maps_source_onto_target(self):
        for target in [(1, 0, 0), (0, 0, -1), D.norm((0.3, 0.5, -0.8)), (0, -1, 1e-9)]:
            q = D.arc((0.0, 1.0, 0.0), D.norm(target))
            self.assertAlmostEqual(sum(c * c for c in q), 1.0, places=9)
            for a, b in zip(rotate(q, (0.0, 1.0, 0.0)), D.norm(target)):
                self.assertAlmostEqual(a, b, places=6)

    def test_limb_quaternion_points_bone_along_direction(self):
        # Rest frame of a root bone: rotation about z taking +y to its rest tail.
        for bone, (rest, _) in D.LIMBS.items():
            theta = math.atan2(-rest[0], rest[1])
            for direction in [(0.9, 0.15, -0.4), (-0.2, 0.75, -0.63), (0.4, -0.9, 0.0)]:
                local = rotate(D.limb_quaternion(bone, direction), (0.0, 1.0, 0.0))
                world = D.rotz(local, theta)
                for a, b in zip(world, D.norm(direction)):
                    self.assertAlmostEqual(a, b, places=6)

    def test_rest_rotation_reproduces_rest_tail(self):
        for rest, _ in D.LIMBS.values():
            tail = D.rotz((0.0, 1.0, 0.0), math.atan2(-rest[0], rest[1]))
            for a, b in zip(tail, D.norm(rest)):
                self.assertAlmostEqual(a, b, places=6)

    def test_pad_keeps_order_and_endpoints(self):
        keys = D.pad([(0.0, 1.0), (8.0, 3.0)], 4)
        self.assertEqual(len(keys), 4)
        self.assertEqual(keys[0], (0.0, 1.0))
        self.assertEqual(keys[-1], (8.0, 3.0))
        self.assertEqual(keys, sorted(keys))


class WizardSpecTest(unittest.TestCase):
    def test_body_is_symmetric_through_the_walk_flip(self):
        # The walk loops a 180-degree flip, so (x, y) -> (-x, -y) must map the
        # body elements onto themselves.
        elements = {tuple(round(v, 9) for v in e) for e in W.BODY['elements']}
        for x, y, z, *rest in W.BODY['elements']:
            self.assertIn(tuple(round(v, 9) for v in [-x, -y, z] + rest), elements)

    def test_body_is_pointed_and_convex(self):
        chain = sorted((e for e in W.BODY['elements'] if e[1] > 0), key=lambda e: e[1])
        radii = [e[3] for e in chain]
        self.assertEqual(radii, sorted(radii, reverse=True))      # narrows toward the tip
        steps = [a - b for a, b in zip(radii, radii[1:])]
        self.assertEqual(steps, sorted(steps))                    # convex: no hat-like flare

    def test_limbs_are_thinner_than_the_warrior(self):
        self.assertLess(W.LIMBS['hand'], 2.4)                     # warrior hands/feet
        self.assertLess(max(W.LIMBS['limb'][1:]), 1.0)            # warrior elbows/knees

    def test_animations_cover_one_cycle_and_mirror_arms(self):
        for entry in W.UNIT['sets']:
            animation = entry['animation'] or {}
            for bone, keys in animation.get('limbs', {}).items():
                self.assertIn(bone, D.LIMBS)
                self.assertEqual((keys[0][0], keys[-1][0]), (1, 9))
                self.assertEqual(keys[0][1], keys[-1][1])         # loops
            limbs = animation.get('limbs', {})
            if 'Bone.004' in limbs:
                for (_, r), (_, l) in zip(limbs['Bone.004'], limbs['Bone.006']):
                    self.assertEqual(l, D.mirror_x(r))

    def test_every_edited_curve_has_enough_keys(self):
        for entry in W.UNIT['sets']:
            if entry['animation']:
                for _, channel, keys, _ in D.pose_curves(entry['animation']):
                    self.assertGreaterEqual(len(keys), D.MIN_KEYS.get(channel, 2), channel)
                    self.assertEqual([k[0] for k in keys], sorted(k[0] for k in keys))


class BlenderScriptTest(unittest.TestCase):
    def test_script_is_python_2_3(self):
        params = D.script_parameters(W.UNIT, W.CAST, '/work/out.blend')
        script = D.BLENDER_SCRIPT % dict(params=D.blender_literal(params))
        for line in script.splitlines():
            code = line.split('#')[0]
            self.assertNotRegex(code, r'\bif\b.*\belse\b(?!:)')   # conditional expressions (2.5+)
            self.assertNotRegex(code, r'\bf["\']')                 # f-strings
            self.assertNotRegex(code, r'^\s*(with|import (math|os|json))\b')
        self.assertNotIn('true', D.blender_literal(params))

    def test_prepare_scene_patches_render_settings(self):
        class FakeRenderer:
            def cpath(self, path):
                return '/work/render'
        with tempfile.TemporaryDirectory() as root:
            scene = Path(root) / 'scene.blend'
            D.prepare_scene(D.ORIGINALS / 'glob-warrior-fight.blend', scene, Path(root), FakeRenderer(), 128, True)
            data = scene.read_bytes()
            schema, _, offset = D.render.schema_and_scene(data)
            fields = schema['RenderData']
            value = lambda f, t: struct.unpack_from('<' + t, data, offset + fields[f][0])[0]
            self.assertEqual((value('xsch', 'h'), value('ysch', 'h')), (128, 128))
            self.assertEqual((value('sfra', 'h'), value('efra', 'h')), (4, 515))
            self.assertEqual(D.team_frames(True)[0], 4)
            self.assertEqual(len(D.team_frames(True)), 512)
            self.assertEqual(len(D.team_frames(False)), 256)


if __name__ == '__main__':
    unittest.main()
