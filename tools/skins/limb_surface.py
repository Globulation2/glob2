# SPDX-License-Identifier: GPL-3.0-or-later
"""Paint-compatible four-limb topology fitted to one connected rest surface.

The two front/back torso panels share four socket rims with the limbs. Each
limb owns its rings and end cap: no triangle can connect two feet. The existing
paint chart and its seams are retained; connected_surface.py builds and carries
the geometry without independently projected collars.
"""

import math
import numpy as np


def unit(v):
    return v / np.linalg.norm(v)


class LimbSurface:
    def __init__(self, definition):
        self.definition = definition
        self.rings = definition["ringsPerSegment"]
        self.divisions = definition["socketDivisions"]
        self.vertices, self.triangles, self.uv, self.regions = [], [], [], []
        self.virtual = []
        # Which components a vertex's field may see (-1: the shared core). The
        # socket rims belong to their limb's paint island but stay on the torso
        # field, so the seam's two copies land on the same surface.
        self.field_regions = []
        self.welds = []
        self.cube = {}
        self.branches = []
        n = self.divisions

        def vertex(q):
            key = tuple(q)
            if key not in self.cube:
                self.cube[key] = len(self.vertices)
                v = unit(np.array(q, dtype=float)) * 1.8
                # Cube axes: depth, upper-right diagonal, upper-left diagonal.
                y, z = (v[1] - v[2]) / math.sqrt(2), (v[1] + v[2]) / math.sqrt(2)
                self.add_vertex(("body", v), (v[0], y, z), -1)
            return self.cube[key]

        # Keep the torso shell around four small socket openings. Using an
        # entire cube face as a socket makes raised/folded arms cave in the body.
        body_grid, opening = 8, 2
        step = 2
        for axis in range(3):
            for sign in (-1, 1):
                normal = np.zeros(3)
                normal[axis] = sign
                u = np.zeros(3)
                u[(axis + 1) % 3] = 1
                v = np.zeros(3)
                v[(axis + 2) % 3] = 1
                for a in range(-body_grid, body_grid, step):
                    for b in range(-body_grid, body_grid, step):
                        if (
                            axis != 0
                            and abs(a + step / 2) < opening
                            and abs(b + step / 2) < opening
                        ):
                            continue
                        corners = [
                            normal * body_grid + u * x + v * y
                            for x, y in (
                                (a, b),
                                (a + step, b),
                                (a + step, b + step),
                                (a, b + step),
                            )
                        ]
                        if sign < 0:
                            corners.reverse()
                        polygon = []
                        for i, q in enumerate(corners):
                            polygon.append(vertex(tuple(q)))
                            midpoint = (q + corners[(i + 1) % 4]) / 2
                            if axis != 0:
                                x, y = np.dot(midpoint, u), np.dot(midpoint, v)
                                if (abs(x) == opening and abs(y) < opening) or (
                                    abs(y) == opening and abs(x) < opening
                                ):
                                    polygon.append(vertex(tuple(midpoint)))
                        self.quad(polygon)
        for branch, path in enumerate(definition["paths"]):
            # Order: upper right, upper left, lower right, lower left.
            axis, sign = ((1, 1), (2, 1), (2, -1), (1, -1))[branch]
            normal = np.zeros(3)
            normal[axis] = sign
            side = np.cross(normal, [1.0, 0.0, 0.0])
            perimeter = (
                [(n, -n + 2 * j) for j in range(n)]
                + [(n - 2 * j, n) for j in range(n)]
                + [(-n, n - 2 * j) for j in range(n)]
                + [(-n + 2 * j, -n) for j in range(n)]
            )
            # The limb's own copy of the socket rim: identical geometry to the
            # torso's rim vertices, welded to them, but with the limb island's
            # paint coordinates so the limb unwraps on its own.
            rim = []
            for x, t in perimeter:
                q = tuple(normal * body_grid + (np.array([x, 0, 0]) + side * t) * opening / n)
                torso_vertex = vertex(q)
                v = unit(np.array(q, dtype=float)) * 1.8
                y, z = (v[1] - v[2]) / math.sqrt(2), (v[1] + v[2]) / math.sqrt(2)
                rim.append(
                    self.add_vertex(("rim", branch, v), (v[0], y, z), branch, -1, torso_vertex)
                )
            angles = np.array([math.atan2(t, x) for x, t in perimeter])
            # These rays lie exactly on the reflection folds, not across them.
            count = (len(path) + 1) * self.rings
            rings = []
            direction = np.array(
                [
                    0.0,
                    (normal[1] - normal[2]) / math.sqrt(2),
                    (normal[1] + normal[2]) / math.sqrt(2),
                ]
            )
            lateral = np.cross(direction, [1.0, 0.0, 0.0])
            for j in range(1, count + 1):
                s = j / count
                ring = []
                for k, angle in enumerate(angles):
                    # A shared virtual rest chart: top/bottom counterpart limbs
                    # have the same UVs even when source proportions differ.
                    virtual = direction * (1.5 + 5.5 * s)
                    virtual += (
                        np.array([1.0, 0.0, 0.0]) * math.cos(angle)
                        + lateral * math.sin(angle)
                    ) * 1.5
                    ring.append(
                        self.add_vertex(("ring", branch, s, k), virtual, branch)
                    )
                self.connect(rim if not rings else rings[-1], ring)
                rings.append(ring)
            # Spherical terminal cap, without a zero-radius ring.
            for j in range(1, 5):
                latitude = j * math.pi / 10
                ring = []
                for k, angle in enumerate(angles):
                    virtual = direction * (1.5 + 5.5 + 1.5 * math.sin(latitude))
                    virtual += (
                        (
                            np.array([1.0, 0.0, 0.0]) * math.cos(angle)
                            + lateral * math.sin(angle)
                        )
                        * 1.5
                        * math.cos(latitude)
                    )
                    ring.append(
                        self.add_vertex(("cap", branch, latitude, k), virtual, branch)
                    )
                self.connect(rings[-1], ring)
                rings.append(ring)
            tip = self.add_vertex(("tip", branch), direction * (1.5 + 7), branch)
            for k in range(len(rim)):
                self.triangles.append(
                    (rings[-1][k], rings[-1][(k + 1) % len(rim)], tip)
                )
            self.branches.append((path, normal, side, angles))
        self.triangles = np.array(self.triangles, dtype=np.uint32)
        self.uv = np.array(self.uv, dtype=np.float64)
        self.regions = np.array(self.regions)
        self.field_regions = np.array(self.field_regions)
        self.welds = np.array(self.welds)

    def add_vertex(self, descriptor, virtual, region, field_region=None, weld=None):
        i = len(self.vertices)
        self.vertices.append(descriptor)
        self.virtual.append(virtual)
        # Provisional planar coordinates; the exporter replaces them with the
        # conformal chart (see chart.py).
        self.uv.append((0.5 + 0.48 * virtual[1] / 9, 0.04 + 0.92 * abs(virtual[2]) / 9))
        self.regions.append(region)
        self.field_regions.append(region if field_region is None else field_region)
        self.welds.append(i if weld is None else weld)
        return i

    def quad(self, q):
        # A center fan is invariant under both reflections. A fixed diagonal
        # would choose opposite interpolation on the mirrored quad.
        virtual = np.mean([self.virtual[i] for i in q], axis=0)
        region = next((self.regions[i] for i in q if self.regions[i] >= 0), -1)
        field_region = next((self.field_regions[i] for i in q if self.field_regions[i] >= 0), -1)
        center = self.add_vertex(("average", tuple(q)), virtual, region, field_region)
        self.triangles.extend(
            (q[k], q[(k + 1) % len(q)], center) for k in range(len(q))
        )

    def connect(self, a, b):
        for k in range(len(a)):
            j = (k + 1) % len(a)
            self.quad((a[k], a[j], b[j], b[k]))

    def contract(self):
        lookup = {
            (tuple(np.round(v, 8)), int(self.regions[i])): i for i, v in enumerate(self.virtual)
        }
        exchange = {0: 2, 1: 3, 2: 0, 3: 1}
        reflections = {}
        for name, axis in (("frontBack", 0), ("topBottom", 2)):
            pairs = []
            for i, v in enumerate(self.virtual):
                other = np.array(v, dtype=float)
                other[axis] *= -1
                region = int(self.regions[i])
                if name == "topBottom":
                    region = exchange.get(region, -1)
                pairs.append(lookup[(tuple(np.round(other, 8)), region)])
            reflections[name] = pairs
        return {
            "reflections": reflections,
            "regions": self.regions.tolist(),
            "welds": self.welds.tolist(),
        }

    def evaluate(self, matrices, radii, stiffness, threshold, clip="walk"):
        from connected_surface import fit_rest
        return fit_rest(self, matrices, radii, stiffness, threshold, clip)

    def body_basis(self, matrices, clip="walk"):
        """Body front/lateral/vertical axes for a clip, as evaluate uses them."""
        root = matrices[0]
        rotation = root[:3, :3] / np.linalg.norm(root[:3, :3], axis=0)
        axes = self.definition.get("clipBodyAxes", {}).get(clip, self.definition["bodyAxes"])
        return np.column_stack([rotation[:, abs(i) - 1] * (1 if i > 0 else -1) for i in axes])

    def fair(self, positions):
        """Relax strained torso triangles with nonshrinking welded fairing.

        The limb rings and caps remain fixed. A shrink/expand pair smooths the
        shared torso shell without shrinking its volume or splitting UV seams.
        """
        iterations = self.definition.get("torsoFairing", 0)
        if not iterations:
            return positions
        if not hasattr(self, "fairing_graph"):
            adjacency = [set() for _ in self.welds]
            for a, b, c in self.welds[self.triangles]:
                for x, y in ((a, b), (b, c), (c, a)):
                    adjacency[x].add(y)
                    adjacency[y].add(x)
            rows = np.array([v for v, neighbours in enumerate(adjacency) for _ in neighbours])
            columns = np.array([k for neighbours in adjacency for k in sorted(neighbours)])
            degree = np.bincount(rows, minlength=len(self.welds))[:, None]
            mask = (self.regions == -1)[:, None] * (degree > 0)
            self.fairing_graph = rows, columns, degree, mask
        rows, columns, degree, mask = self.fairing_graph
        result = positions.copy()
        for _ in range(iterations):
            for rate in (0.5, -0.53):
                total = np.zeros_like(result)
                np.add.at(total, rows, result[columns])
                result += rate * (total / np.maximum(degree, 1) - result) * mask
                result = result[self.welds]
        return result

    def normals(self, positions):
        """Shade the actual connected geometry, including welded paint seams."""
        triangles = self.triangles
        face = np.cross(positions[triangles[:, 1]] - positions[triangles[:, 0]],
                        positions[triangles[:, 2]] - positions[triangles[:, 0]])
        result = np.zeros_like(positions)
        for corner in range(3):
            np.add.at(result, self.welds[triangles[:, corner]], face)
        result = result[self.welds]
        lengths = np.linalg.norm(result, axis=1)
        if np.any(lengths < 1e-10):
            raise ValueError("Degenerate connected surface normal")
        return result / lengths[:, None]


class Tracker:
    """Carry one connected rest surface through every legacy animation pose."""

    def __init__(self, surface, rest, radii, stiffness, threshold, clip="walk"):
        self.surface, self.radii = surface, radii
        self.rest_basis = surface.body_basis(rest, clip)
        self.rest_origin = rest[0, :3, 3]
        self.rest_centers = (rest[:, :3, 3] - self.rest_origin) @ self.rest_basis
        self.rest_scales = np.linalg.norm(rest[:, :3, :3], axis=1).mean(axis=1)
        self.positions = surface.evaluate(rest, radii, stiffness, threshold, clip)
        self.local = (self.positions - self.rest_origin) @ self.rest_basis
        self.samples = {}

    def evaluate(self, matrices, clip="walk", phase=None):
        from connected_surface import carry
        basis = self.surface.body_basis(matrices, clip)
        origin = matrices[0, :3, 3]
        centers = (matrices[:, :3, 3] - origin) @ basis
        scales = np.linalg.norm(matrices[:, :3, :3], axis=1).mean(axis=1)
        cached = self.samples.get((clip, phase)) if phase is not None else None
        if cached is not None and np.allclose(centers, cached[0], atol=2e-4, rtol=0) and np.allclose(scales, cached[1], atol=2e-4, rtol=0):
            local = cached[2]
        else:
            local = carry(self.local, self.rest_centers, centers, self.rest_scales, scales, self.radii)
            if phase is not None:
                # Headings differ only by the outer body transform. Reuse the
                # body-space sample when float authoring noise is below 0.0002.
                self.samples[(clip, phase)] = (centers.copy(), scales.copy(), local)

        return self.surface.fair(origin + local @ basis.T)
