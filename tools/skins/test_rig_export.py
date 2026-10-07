# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with Blender --background --python-exit-code 1 --python this_file."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mathutils import Matrix
from export_rig import encode, transform


class RigExportTest(unittest.TestCase):
    def test_uniform_trs_round_trip(self):
        value = Matrix.Translation((1, -2, 3)) @ Matrix.Rotation(0.7, 4, "Z") @ Matrix.Scale(2, 4)
        result = transform(value)
        self.assertEqual(result[:3], [1, -2, 3])
        self.assertAlmostEqual(result[-1], 2, places=5)
        self.assertAlmostEqual(sum(v * v for v in result[3:7]), 1, places=5)

    def test_nonuniform_scale_reflection_and_shear_are_rejected(self):
        for value in (
            Matrix.Diagonal((1, 2, 1, 1)),
            Matrix.Diagonal((-1, 1, 1, 1)),
            Matrix.Diagonal((0, 1, 1, 1)),
            Matrix(((1, 0.2, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))),
        ):
            with self.subTest(matrix=value), self.assertRaises(ValueError):
                transform(value)

    def model(self):
        identity = Matrix.Identity(4)
        vertices = [
            ([0, 0, 0], [0, 0, 1], [0, 0], [0, 0, 0, 0], [1, 0, 0, 0]),
            ([1, 0, 0], [0, 0, 1], [1, 0], [0, 0, 0, 0], [1, 0, 0, 0]),
            ([0, 1, 0], [0, 0, 1], [0, 1], [0, 0, 0, 0], [1, 0, 0, 0]),
        ]
        clip = {
            "id": 0,
            "duration": 1,
            "modelToClip": [v for row in identity for v in row],
            "normalToCamera": [1, 0, 0, 0, 1, 0, 0, 0, 1],
            "pivot": [0, 0, 0],
            "radius": 1,
            "frames": [(0, 0)] * 256,
            "tracks": [[identity]],
        }
        return vertices, [0, 1, 2], [(0xFFFFFFFF, identity, identity)], [clip], 38

    def test_encodes_complete_model_and_binary32_endpoints(self):
        import struct

        model = self.model()
        data = encode(*model)
        magic, vertices, indices, bones, clips, size, length = struct.unpack_from("<4s6I", data)
        self.assertEqual((magic, vertices, indices, bones, clips, size), (b"GSR1", 3, 3, 1, 1, 38))
        self.assertEqual(length, len(data) - 28)
        # Header, 3 vertices, 3 indices, 1 bone, clip header with 256 frame
        # mappings, and one track key.
        self.assertEqual(len(data), 28 + 3 * 64 + 3 * 4 + 68 + 2176 + 32)
        model[3][0]["duration"] = 0.0001
        model[3][0]["tracks"][0][0] = Matrix.Scale(0.0001, 4)
        self.assertEqual(encode(*model)[:4], b"GSR1")

    def test_camera_metadata_is_validated_before_serialization(self):
        bad_values = [
            ("modelToClip", [1] * 15),
            ("modelToClip", [0] * 16),
            ("normalToCamera", [1] * 8),
            ("normalToCamera", [1] * 9),
            ("pivot", [0, 0]),
            ("radius", 0),
            ("duration", 0.00001),
        ]
        for field, value in bad_values:
            model = self.model()
            model[3][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                encode(*model)

    def test_duplicate_clip_ids_and_bad_inverse_binds_are_rejected(self):
        model = self.model()
        model[3].append(deepcopy(model[3][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            encode(*model)
        model = self.model()
        model[2][0] = (0xFFFFFFFF, Matrix.Translation((1, 0, 0)), Matrix.Identity(4))
        with self.assertRaisesRegex(ValueError, "inverse bind"):
            encode(*model)

    def test_rounding_cannot_move_frame_time_to_the_excluded_cycle_end(self):
        model = self.model()
        # Both authoring values are distinct doubles, but serialize to 1.0f.
        model[3][0]["frames"][0] = (0, 1 - 1e-9)
        with self.assertRaisesRegex(ValueError, "frame mapping"):
            encode(*model)

    def test_hierarchy_scale_products_are_bounded(self):
        model = self.model()
        identity = Matrix.Identity(4)
        model[2].append((0, identity, identity))
        model[3][0]["tracks"] = [[Matrix.Scale(100, 4), Matrix.Scale(101, 4)]]
        with self.assertRaisesRegex(ValueError, "hierarchy"):
            encode(*model)


if __name__ == "__main__":
    if (
        not unittest.TextTestRunner()
        .run(unittest.defaultTestLoader.loadTestsFromTestCase(RigExportTest))
        .wasSuccessful()
    ):
        raise RuntimeError("Rig export contract failed")
