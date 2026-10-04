# SPDX-License-Identifier: GPL-3.0-or-later
"""Authored four-socket surface, evaluated from named legacy component paths.

The two front/back torso panels share four socket rims with the limbs. Each
limb owns its rings and end cap: no triangle can connect two feet. Ring radii
follow only the torso and that limb's metaball field, never a neighbouring limb.
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
            rim = [
                vertex(
                    tuple(
                        normal * body_grid
                        + (np.array([x, 0, 0]) + side * t) * opening / n
                    )
                )
                for x, t in perimeter
            ]
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

    def add_vertex(self, descriptor, virtual, region):
        i = len(self.vertices)
        self.vertices.append(descriptor)
        self.virtual.append(virtual)
        self.uv.append((0.5 + 0.48 * virtual[1] / 9, 0.04 + 0.92 * abs(virtual[2]) / 9))
        self.regions.append(region)
        return i

    def quad(self, q):
        # A center fan is invariant under both reflections. A fixed diagonal
        # would choose opposite interpolation on the mirrored quad.
        virtual = np.mean([self.virtual[i] for i in q], axis=0)
        region = next((self.regions[i] for i in q if self.regions[i] >= 0), -1)
        center = self.add_vertex(("average", tuple(q)), virtual, region)
        self.triangles.extend(
            (q[k], q[(k + 1) % len(q)], center) for k in range(len(q))
        )

    def connect(self, a, b):
        for k in range(len(a)):
            j = (k + 1) % len(a)
            self.quad((a[k], a[j], b[j], b[k]))

    def contract(self):
        lookup = {tuple(np.round(v, 8)): i for i, v in enumerate(self.virtual)}
        reflections = {}
        for name, axis in (("frontBack", 0), ("topBottom", 2)):
            pairs = []
            for v in self.virtual:
                other = np.array(v, dtype=float)
                other[axis] *= -1
                pairs.append(lookup[tuple(np.round(other, 8))])
            reflections[name] = pairs
        return {"reflections": reflections, "regions": self.regions.tolist()}

    def evaluate(self, matrices, radii, stiffness, threshold, clip="walk"):
        root = matrices[0]
        rotation = root[:3, :3] / np.linalg.norm(root[:3, :3], axis=0)
        axes = self.definition.get("clipBodyAxes", {}).get(
            clip, self.definition["bodyAxes"]
        )
        basis = np.column_stack(
            [rotation[:, abs(i) - 1] * (1 if i > 0 else -1) for i in axes]
        )
        # Transform our diagonal cube axes into body front/lateral/vertical axes.
        cube_basis = basis @ np.array(
            [
                [1, 0, 0],
                [0, 1 / math.sqrt(2), -1 / math.sqrt(2)],
                [0, 1 / math.sqrt(2), 1 / math.sqrt(2)],
            ]
        )
        scales = np.linalg.norm(matrices[:, :3, :3], axis=1).mean(axis=1)
        support = radii * scales
        isolated = support * np.sqrt(1 - (threshold / stiffness) ** (1 / 3))
        body_radius = isolated[0] * self.definition["bodyRadiusScale"]
        results = []
        cache = {}
        solved = {}
        for b, (path, normal, side, angles) in enumerate(self.branches):
            ids = [0] + path
            center = root[:3, 3] + cube_basis @ normal * (
                body_radius / math.sqrt(1 + (2 / 8) ** 2)
            )
            controls = np.vstack((center, matrices[path, :3, 3]))

            # Ring sections use a centripetal-free cubic Hermite path; each
            # segment is parameterized separately so source joints keep identity.
            def curve(s, controls=controls):
                t = s * (len(controls) - 1)
                i = min(int(t), len(controls) - 2)
                t -= i
                p0, p1 = controls[i : i + 2]
                m0 = (
                    controls[min(i + 1, len(controls) - 1)] - controls[max(i - 1, 0)]
                ) / (1 if i == 0 else 2)
                m1 = (controls[min(i + 2, len(controls) - 1)] - controls[i]) / (
                    1 if i + 1 == len(controls) - 1 else 2
                )
                c = (
                    (2 * t**3 - 3 * t * t + 1) * p0
                    + (t**3 - 2 * t * t + t) * m0
                    + (-2 * t**3 + 3 * t * t) * p1
                    + (t**3 - t * t) * m1
                )
                tangent = unit(
                    (6 * t * t - 6 * t) * p0
                    + (3 * t * t - 4 * t + 1) * m0
                    + (-6 * t * t + 6 * t) * p1
                    + (3 * t * t - 2 * t) * m1
                )
                return c, tangent

            # Parallel transport around bends. Projecting the body's front axis
            # independently at each ring flips the cross section when a fighting
            # arm becomes parallel to that axis, making a bow-tie connection.
            section_frames = {}
            previous = cube_basis @ normal
            front = basis[:, 0].copy()
            count = (len(path) + 1) * self.rings
            for sample in range(count * 4 + 1):
                s = sample / (count * 4)
                c, tangent = curve(s)
                k = np.cross(previous, tangent)
                cosine = float(np.dot(previous, tangent))
                if cosine > -1 + 1e-8:
                    front += np.cross(k, front) + np.cross(k, np.cross(k, front)) / (
                        1 + cosine
                    )
                front = unit(front - tangent * np.dot(front, tangent))
                previous = tangent
                if sample % 4 == 0:
                    section_frames[s] = (
                        c,
                        tangent,
                        front.copy(),
                        np.cross(tangent, front),
                    )

            def section(s, frames=section_frames):
                return frames[s]

            perimeter = (
                [
                    (self.divisions, -self.divisions + 2 * j)
                    for j in range(self.divisions)
                ]
                + [
                    (self.divisions - 2 * j, self.divisions)
                    for j in range(self.divisions)
                ]
                + [
                    (-self.divisions, self.divisions - 2 * j)
                    for j in range(self.divisions)
                ]
                + [
                    (-self.divisions + 2 * j, -self.divisions)
                    for j in range(self.divisions)
                ]
            )
            rim = np.array(
                [
                    root[:3, 3]
                    + cube_basis
                    @ unit(
                        normal * 8
                        + (np.array([x, 0, 0]) + side * t) * 2 / self.divisions
                    )
                    * body_radius
                    for x, t in perimeter
                ]
            )
            cache[b] = (ids, section, isolated[path[-1]], rim, center)
        for descriptor in self.vertices:
            kind = descriptor[0]
            if kind == "average":
                results.append(np.mean([results[i] for i in descriptor[1]], axis=0))
                continue
            if kind == "body":
                results.append(
                    root[:3, 3] + cube_basis @ descriptor[1] * (body_radius / 1.8)
                )
                continue
            b = descriptor[1]
            ids, section, tip_radius, rim, socket_center = cache[b]
            if kind == "ring":
                _, _, s, k = descriptor
                if (b, s) not in solved:
                    c, tangent, front, lateral = section(s)
                    angles = self.branches[b][3]
                    rays = (
                        np.cos(angles)[:, None] * front
                        + np.sin(angles)[:, None] * lateral
                    )
                    lo = np.zeros(len(angles))
                    hi = np.full(len(angles), max(support[ids]) * 2)
                    for _ in range(24):
                        r = (lo + hi) / 2
                        distance = (
                            np.sum(
                                (
                                    c
                                    + rays[:, None, :] * r[:, None, None]
                                    - matrices[ids, :3, 3]
                                )
                                ** 2,
                                axis=2,
                            )
                            / support[ids] ** 2
                        )
                        field = np.sum(
                            stiffness[ids] * np.maximum(0, 1 - distance) ** 3, axis=1
                        )
                        lo = np.where(field > threshold, r, lo)
                        hi = np.where(field > threshold, hi, r)
                    # A branch can fold outside its own implicit field while
                    # remaining joined in the all-limb legacy union. Keep an
                    # authored neck there instead of a near-zero-radius spike.
                    path = self.branches[b][0]
                    t = s * len(path)
                    segment = min(int(t), len(path) - 1)
                    t -= segment
                    necks = np.r_[body_radius * 0.25, isolated[path]]
                    minimum = (
                        (1 - t) * necks[segment] + t * necks[segment + 1]
                    ) * self.definition["minimumSectionRadiusScale"]
                    radius = np.maximum((lo + hi) / 2, minimum)
                    solved[b, s] = c + rays * radius[:, None]
                    blend = min(1.0, s * len(self.branches[b][0]))
                    if blend < 1:
                        # Start at the exact shared socket rim. This avoids an
                        # instant field-radius jump intersecting adjacent sockets.
                        solved[b, s] = (1 - blend) * (
                            rim + c - socket_center
                        ) + blend * solved[b, s]
                results.append(solved[b, s][k])
            else:
                c, tangent, front, lateral = section(1.0)
                if kind == "tip":
                    results.append(c + tangent * tip_radius)
                else:
                    _, _, latitude, k = descriptor
                    angle = self.branches[b][3][k]
                    results.append(
                        c
                        + tangent * tip_radius * math.sin(latitude)
                        + (front * math.cos(angle) + lateral * math.sin(angle))
                        * tip_radius
                        * math.cos(latitude)
                    )
        positions = np.array(results)
        # Fit the shared core and each explicit limb to their local implicit
        # surface. This removes the faceted/intersecting collar left by a radial
        # sweep when joints fold against the torso, without welding two feet.
        allowed = np.zeros((len(positions), len(matrices)), dtype=bool)
        core = [0] + [path[0] for path, *_ in self.branches]
        allowed[:, core] = True
        for branch, (path, *_) in enumerate(self.branches):
            allowed[np.ix_(self.regions == branch, path)] = True
        self.field_centers = matrices[:, :3, 3]
        self.field_support = support
        self.field_stiffness = stiffness
        self.field_allowed = allowed
        for _ in range(10):
            field, gradient = self.field(positions)
            divisor = np.sum(gradient * gradient, axis=1)
            delta = (
                gradient * ((field - threshold) / np.maximum(divisor, 1e-12))[:, None]
            )
            length = np.linalg.norm(delta, axis=1)
            delta *= np.minimum(1.0, body_radius * 0.12 / np.maximum(length, 1e-12))[
                :, None
            ]
            positions -= delta
        return positions

    def field(self, positions):
        delta = positions[:, None, :] - self.field_centers
        distance = np.sum(delta * delta, axis=2) / self.field_support**2
        falloff = np.maximum(0.0, 1 - distance) * self.field_allowed
        field = np.sum(self.field_stiffness * falloff**3, axis=1)
        gradient = np.sum(
            (-6 * self.field_stiffness * falloff**2 / self.field_support**2)[:, :, None]
            * delta,
            axis=1,
        )
        return field, gradient

    def normals(self, positions):
        _, gradient = self.field(positions)
        lengths = np.linalg.norm(gradient, axis=1)
        if np.any(lengths < 1e-10):
            raise ValueError("Invalid implicit surface normal")
        return -gradient / lengths[:, None]
