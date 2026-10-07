# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit the existing paint topology to one connected implicit rest surface.

A shared convex disk parameterization transfers the old socket topology onto
one front/top panel. Reflecting that panel closes the mesh without overlapping
collars. The atlas coordinates remain those of the established paint layout.
"""

from collections import Counter
import numpy as np


def disk(vertices, triangles, welds):
    """Weld one quarter and embed its disk with a convex boundary."""
    vertices = np.asarray(vertices)
    inside = (vertices[:, 0] >= -1e-9) & (vertices[:, 2] >= -1e-9)
    triangles = welds[triangles[np.all(inside[triangles], axis=1)]]
    members = np.unique(triangles)
    lookup = {v: i for i, v in enumerate(members)}
    triangles = np.array([[lookup[v] for v in t] for t in triangles])
    q = vertices[members]
    edges = Counter(tuple(sorted((a, b))) for t in triangles for a, b in zip(t, np.roll(t, -1)))
    if not edges or not set(edges.values()) <= {1, 2}:
        raise ValueError("Quarter is not a manifold disk")
    boundary = {v for edge, count in edges.items() if count == 1 for v in edge}
    neighbours = [set() for _ in members]
    perimeter = {v: set() for v in boundary}
    for (a, b), count in edges.items():
        neighbours[a].add(b)
        neighbours[b].add(a)
        if count == 1:
            perimeter[a].add(b)
            perimeter[b].add(a)
    if any(len(n) != 2 for n in perimeter.values()):
        raise ValueError("Quarter boundary is not a single loop")
    diameter = [v for v in boundary if abs(q[v, 2]) < 1e-8]
    start, end = max(diameter, key=lambda v: q[v, 1]), min(diameter, key=lambda v: q[v, 1])
    chain, before, at = [start], None, start
    while at != end or len(chain) == 1:
        choices = [v for v in perimeter[at] if v != before]
        if before is None:
            choices.sort(key=lambda v: q[v, 2], reverse=True)
        before, at = at, choices[0]
        if at in chain:
            raise ValueError("Quarter perimeter did not reach the diameter")
        chain.append(at)
    lengths = np.r_[0, np.cumsum(np.linalg.norm(np.diff(q[chain], axis=0), axis=1))]
    angles = lengths / lengths[-1] * np.pi
    uv = np.zeros((len(members), 2))
    uv[chain] = np.c_[np.cos(angles), np.sin(angles)]
    radius = max(abs(q[diameter, 1]))
    for v in boundary - set(chain):
        uv[v] = [q[v, 1] / radius, 0]
    free = sorted(set(range(len(members))) - boundary)
    indices = {v: i for i, v in enumerate(free)}
    a, b = np.zeros((len(free), len(free))), np.zeros((len(free), 2))
    for v, i in indices.items():
        a[i, i] = len(neighbours[v])
        for j in neighbours[v]:
            if j in indices:
                a[i, indices[j]] -= 1
            else:
                b[i] += uv[j]
    uv[free] = np.linalg.solve(a, b)
    signed = np.cross(uv[triangles[:, 1]] - uv[triangles[:, 0]], uv[triangles[:, 2]] - uv[triangles[:, 0]])
    if not (np.all(signed > 1e-12) or np.all(signed < -1e-12)):
        raise ValueError("Convex quarter embedding folded")
    return uv, members, lookup, triangles, boundary, chain, angles


def quarter(definition, centers, support, stiffness, threshold, step=0.3):
    """One height-field panel of the rest shape; distal limbs stay separate."""
    core = [0] + [path[0] for path in definition["paths"]]
    masks = [sorted(set(core + path)) for path in definition["paths"]]

    def value(q):
        result = 0.0
        for sign in (-1, 1):
            delta = q - centers * [1, 1, sign]
            falloff = stiffness * np.maximum(0, 1 - np.sum(delta * delta, axis=-1) / support**2)**3
            result += max(falloff[mask].sum() for mask in masks) / 2
        return result - threshold

    bound = np.max(np.abs(centers[:, 1:]) + support[:, None], axis=0) + step
    ny, nz = np.ceil(bound / step).astype(int)
    grid = {(i, j): np.array([0., i * step, j * step]) for i in range(-ny, ny + 1) for j in range(nz + 1)}
    values = {key: value(q) for key, q in grid.items()}
    points, triangles, cache = [], [], {}

    def corner(key):
        tag = ("corner", key)
        if tag not in cache:
            q = grid[key].copy()
            lo, hi = 0., float(max(support) * 2)
            for _ in range(32):
                mid = (lo + hi) / 2
                q[0] = mid
                if value(q) > 0:
                    lo = mid
                else:
                    hi = mid
            q[0] = (lo + hi) / 2
            cache[tag] = len(points)
            points.append(q)
        return cache[tag]

    def crossing(a, b):
        tag = ("edge", tuple(sorted((a, b))))
        if tag not in cache:
            inside, outside = (a, b) if values[a] > 0 else (b, a)
            lo, hi = grid[inside].copy(), grid[outside].copy()
            for _ in range(32):
                mid = (lo + hi) / 2
                if value(mid) > 0:
                    lo = mid
                else:
                    hi = mid
            cache[tag] = len(points)
            points.append((lo + hi) / 2)
        return cache[tag]

    for i in range(-ny, ny):
        for j in range(nz):
            a, b, c, d = (i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)
            for tri in ((a, b, c), (a, c, d)):
                polygon = []
                for k, key in enumerate(tri):
                    nxt = tri[(k + 1) % 3]
                    if values[key] > 0:
                        polygon.append(corner(key))
                    if (values[key] > 0) != (values[nxt] > 0):
                        polygon.append(crossing(key, nxt))
                triangles.extend((polygon[0], polygon[k], polygon[k + 1]) for k in range(1, len(polygon) - 1))
    return np.array(points), np.array(triangles, dtype=np.uint32)


def fit_rest(surface, matrices, radii, stiffness, threshold, clip):
    from mathutils.bvhtree import BVHTree

    basis = surface.body_basis(matrices, clip)
    origin = matrices[0, :3, 3]
    centers = (matrices[:, :3, 3] - origin) @ basis
    support = radii * np.linalg.norm(matrices[:, :3, :3], axis=1).mean(axis=1)
    target, faces = quarter(surface.definition, centers, support, stiffness, threshold)
    target_uv, tmembers, _, ttri, tb, tc, ta = disk(target, faces, np.arange(len(target)))
    uv, members, lookup, _, boundary, chain, angles = disk(surface.virtual, surface.triangles, surface.welds)
    tree = BVHTree.FromPolygons(np.c_[target_uv, np.zeros(len(target_uv))].tolist(), ttri.tolist(), all_triangles=True)
    mapped = np.zeros((len(members), 3))
    for i, point in enumerate(uv):
        near, _, face, _ = tree.find_nearest((point[0], point[1], 0))
        tri = ttri[face]
        weights = np.linalg.solve(np.vstack((target_uv[tri].T, np.ones(3))), np.r_[near[:2], 1])
        mapped[i] = weights @ target[tmembers[tri]]
    # The disk has polygonal chords. Place boundary samples on the actual
    # target perimeter to keep both reflection folds exactly welded.
    for v, angle in zip(chain, angles):
        index = min(len(ta) - 2, max(0, np.searchsorted(ta, angle) - 1))
        alpha = (angle - ta[index]) / (ta[index + 1] - ta[index])
        mapped[v] = (1 - alpha) * target[tmembers[tc[index]]] + alpha * target[tmembers[tc[index + 1]]]
    diameter = [tc[-1]] + sorted(tb - set(tc), key=lambda i: target_uv[i, 0]) + [tc[0]]
    values = target_uv[diameter, 0]
    for v in boundary - set(chain):
        index = min(len(values) - 2, max(0, np.searchsorted(values, uv[v, 0]) - 1))
        alpha = (uv[v, 0] - values[index]) / (values[index + 1] - values[index])
        mapped[v] = (1 - alpha) * target[tmembers[diameter[index]]] + alpha * target[tmembers[diameter[index + 1]]]
    virtual = np.array(surface.virtual)
    reflections = surface.contract()["reflections"]
    positions = np.zeros_like(virtual)
    for v in range(len(virtual)):
        representative = v
        if virtual[v, 0] < -1e-9:
            representative = reflections["frontBack"][representative]
        if virtual[v, 2] < -1e-9:
            representative = reflections["topBottom"][representative]
        point = mapped[lookup[surface.welds[representative]]].copy()
        if virtual[v, 0] < -1e-9:
            point[0] *= -1
        if virtual[v, 2] < -1e-9:
            point[2] *= -1
        positions[v] = point
    return origin + positions @ basis.T


def carry(positions, rest_centers, goal_centers, rest_scales, goal_scales, radii, steps=64, width=0.65):
    """Integrate locally rigid motion, leaving soft blends between components.

    At each point, fit translation and angular velocity to nearby moving
    component centers. This removes the artificial shear of a translation-only
    blend on the larger bulbs. Positive Gaussian weights and midpoint steps
    keep the connecting surface continuous through close limb poses.
    """
    velocity = goal_centers - rest_centers
    p = positions.copy()

    def speed(q, t, rigid=True):
        centers = rest_centers + t * velocity
        scales = rest_scales * (1 - t) + goal_scales * t
        delta = q[:, None, :] - centers
        logits = -np.sum(delta**2, axis=2) / (radii * scales * width)**2
        logits -= logits.max(axis=1, keepdims=True)
        weights = np.exp(logits)
        weights /= weights.sum(axis=1, keepdims=True)
        if not rigid:
            local = velocity + delta * ((goal_scales - rest_scales) / scales)[None, :, None]
            return np.sum(local * weights[:, :, None], axis=1)
        center = weights @ centers
        translation = weights @ velocity
        offsets = centers[None, :, :] - center[:, None, :]
        covariance = np.einsum("ni,nij,nik->njk", weights, offsets, offsets)
        inertia = np.trace(covariance, axis1=1, axis2=2)[:, None, None] * np.eye(3) - covariance
        # Planar authoring poses can leave one angular direction unconstrained.
        inertia += np.eye(3) * 1e-5
        moment = np.sum(weights[:, :, None] * np.cross(offsets, velocity), axis=1)
        angular = np.linalg.solve(inertia, moment[:, :, None])[:, :, 0]
        dilation = weights @ ((goal_scales - rest_scales) / scales)
        relative = q - center
        return translation + np.cross(angular, relative) + relative * dilation[:, None]

    for k in range(steps):
        t = k / steps
        midpoint = p + speed(p, t) * (0.5 / steps)
        p += speed(midpoint, t + 0.5 / steps) / steps
    # A small compliant contribution relaxes the tightly bent neck triangles
    # without losing the roundness of the larger components.
    compliant = positions.copy()
    for k in range(32):
        t = k / 32
        midpoint = compliant + speed(compliant, t, False) / 64
        compliant += speed(midpoint, t + 1 / 64, False) / 32
    return p * 0.9 + compliant * 0.1
