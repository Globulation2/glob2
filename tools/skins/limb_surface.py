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
            if kind == "rim":
                results.append(
                    root[:3, 3] + cube_basis @ descriptor[2] * (body_radius / 1.8)
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
        self.prepare_field(matrices, radii, stiffness)
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

    def body_basis(self, matrices, clip="walk"):
        """Body front/lateral/vertical axes for a clip, as evaluate uses them."""
        root = matrices[0]
        rotation = root[:3, :3] / np.linalg.norm(root[:3, :3], axis=0)
        axes = self.definition.get("clipBodyAxes", {}).get(clip, self.definition["bodyAxes"])
        return np.column_stack([rotation[:, abs(i) - 1] * (1 if i > 0 else -1) for i in axes])

    def prepare_field(self, matrices, radii, stiffness):
        """Set the implicit field of one pose: every vertex sees the shared core
        (body and proximal components) and its own limb's components only."""
        scales = np.linalg.norm(matrices[:, :3, :3], axis=1).mean(axis=1)
        allowed = np.zeros((len(self.vertices), len(matrices)), dtype=bool)
        core = [0] + [path[0] for path, *_ in self.branches]
        allowed[:, core] = True
        for branch, (path, *_) in enumerate(self.branches):
            allowed[np.ix_(self.field_regions == branch, path)] = True
        self.field_centers = matrices[:, :3, 3]
        self.field_support = radii * scales
        self.field_stiffness = stiffness
        self.field_allowed = allowed

    def snap(self, positions, threshold, limit, iterations=12):
        """Move positions onto the prepared field's isosurface along its gradient."""
        positions = np.array(positions, dtype=float, copy=True)
        for _ in range(iterations):
            field, gradient = self.field(positions)
            divisor = np.maximum(np.sum(gradient * gradient, axis=1), 1e-12)
            delta = gradient * ((field - threshold) / divisor)[:, None]
            length = np.linalg.norm(delta, axis=1)
            delta *= np.minimum(1.0, limit / np.maximum(length, 1e-12))[:, None]
            positions -= delta
        return positions

    def symmetrised(self, positions, matrices, clip="walk"):
        """Average a surface with its chart reflections in the body frame."""
        basis = self.body_basis(matrices, clip)
        centre = matrices[0, :3, 3]
        local = (positions - centre) @ basis
        reflections = self.contract()["reflections"]
        images = [local]
        for axis, name in ((0, "frontBack"), (2, "topBottom")):
            mirrored = local[reflections[name]].copy()
            mirrored[:, axis] *= -1
            images.append(mirrored)
        both = local[np.array(reflections["frontBack"])[reflections["topBottom"]]].copy()
        both[:, [0, 2]] *= -1
        images.append(both)
        return centre + np.mean(images, axis=0) @ basis.T

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


class Tracker:
    """Per-frame surfaces with a stable vertex correspondence.

    ``LimbSurface.evaluate`` re-solves the chart every frame, so a vertex is
    tied to the chart's parameters rather than to a point on the body: paint
    swims as limbs move, and collars grow over the torso in overlapping
    layers. The tracker instead carries the rest surface with one similarity
    transform per component (the body's orientation for the body, the
    segment's swing for limb components), blended by each vertex's share of
    the components' field at rest, and then snaps every vertex onto the posed
    implicit surface along its gradient. Vertices stay on the same part of the
    body, the surface is still the metaball union, and no layers overlap.
    """

    def __init__(self, surface, rest, radii, stiffness, threshold, clip="walk"):
        self.surface = surface
        self.radii, self.stiffness, self.threshold = radii, stiffness, threshold
        self.rest = rest
        self.rest_basis = surface.body_basis(rest, clip)
        positions = surface.evaluate(rest, radii, stiffness, threshold, clip)
        self.positions = surface.symmetrised(positions, rest, clip)
        surface.prepare_field(rest, radii, stiffness)
        delta = self.positions[:, None, :] - surface.field_centers
        distance = np.sum(delta * delta, axis=2) / surface.field_support**2
        contribution = surface.field_stiffness * np.maximum(0, 1 - distance) ** 3
        contribution = contribution * surface.field_allowed
        self.weights = contribution / np.maximum(contribution.sum(axis=1, keepdims=True), 1e-12)
        for v, descriptor in enumerate(surface.vertices):
            if descriptor[0] == "average":
                self.weights[v] = self.weights[list(descriptor[1])].mean(axis=0)
        self.previous = {}
        for path, *_ in surface.branches:
            for k, part in enumerate(path):
                self.previous[part] = 0 if k == 0 else path[k - 1]
        scales = np.linalg.norm(rest[:, :3, :3], axis=1).mean(axis=1)
        body_radius = radii[0] * scales[0] * math.sqrt(1 - (threshold / stiffness[0]) ** (1 / 3))
        self.limit = body_radius * 0.12
        self.bind = self.components(rest, self.rest_basis)
        self.inverse_bind = np.linalg.inv(self.bind)
        self.homogeneous = np.concatenate((self.positions, np.ones((len(self.positions), 1))), axis=1)

    def components(self, matrices, basis):
        """One similarity transform per component for a pose."""
        scales = np.linalg.norm(matrices[:, :3, :3], axis=1).mean(axis=1)
        rest_scales = np.linalg.norm(self.rest[:, :3, :3], axis=1).mean(axis=1)
        result = np.zeros((len(matrices), 4, 4))
        for part in range(len(matrices)):
            rotation = basis
            if part in self.previous:
                before = self.previous[part]
                at_rest = self.rest_basis.T @ (self.rest[part, :3, 3] - self.rest[before, :3, 3])
                posed = basis.T @ (matrices[part, :3, 3] - matrices[before, :3, 3])
                rotation = basis @ minimal_rotation(at_rest, posed)
            result[part, :3, :3] = rotation * (scales[part] / rest_scales[part])
            result[part, :3, 3] = matrices[part, :3, 3]
            result[part, 3, 3] = 1
        return result

    def evaluate(self, matrices, clip="walk"):
        basis = self.surface.body_basis(matrices, clip)
        relative = self.components(matrices, basis) @ self.inverse_bind
        guess = np.einsum("bij,vj,vb->vi", relative[:, :3, :], self.homogeneous, self.weights)
        self.surface.prepare_field(matrices, self.radii, self.stiffness)
        return self.surface.snap(guess, self.threshold, self.limit)


def minimal_rotation(source, target):
    """Rotation matrix taking direction ``source`` to ``target`` by the shortest arc."""
    a = source / max(np.linalg.norm(source), 1e-12)
    b = target / max(np.linalg.norm(target), 1e-12)
    axis = np.cross(a, b)
    sine = np.linalg.norm(axis)
    cosine = float(np.clip(a @ b, -1, 1))
    if sine < 1e-12:
        if cosine > 0:
            return np.eye(3)
        # Opposite directions: turn half a circle about any perpendicular axis.
        perpendicular = np.cross(a, [1.0, 0.0, 0.0])
        if np.linalg.norm(perpendicular) < 1e-6:
            perpendicular = np.cross(a, [0.0, 1.0, 0.0])
        axis = perpendicular / np.linalg.norm(perpendicular)
        sine, cosine = 0.0, -1.0
    else:
        axis = axis / sine
    k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    return np.eye(3) + sine * k + (1 - cosine) * (k @ k)
