#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Basic motion invariants, independent of Blender and the authored meshes."""
import unittest
import numpy as np
from connected_surface import carry


class ConnectedMotionTest(unittest.TestCase):
    def setUp(self):
        rng = np.random.default_rng(19)
        self.centers = rng.normal(size=(5, 3))
        self.points = rng.normal(size=(48, 3))
        self.scales = np.ones(5)
        self.radii = np.full(5, 2.)

    def deform(self, points, rest, goal):
        return carry(points, rest, goal, self.scales, self.scales, self.radii)

    def test_stationary_shape_and_uniform_translation_preserve_distances(self):
        for translation in (np.zeros(3), np.array([1.3, -.7, .2])):
            actual = self.deform(self.points, self.centers, self.centers + translation)
            np.testing.assert_allclose(actual, self.points + translation, atol=1e-10, rtol=0)

    def test_result_commutes_with_body_rotation_and_reflection(self):
        goal = self.centers + np.random.default_rng(23).normal(size=(5, 3)) * .3
        reference = self.deform(self.points, self.centers, goal)
        for transform in (np.array([[0., -1., 0.], [1., 0., 0.], [0., 0., 1.]]), np.diag([-1., 1., 1.])):
            actual = self.deform(self.points @ transform.T, self.centers @ transform.T, goal @ transform.T)
            np.testing.assert_allclose(actual, reference @ transform.T, atol=1e-10, rtol=0)


if __name__ == '__main__':
    unittest.main()
