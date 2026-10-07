# SPDX-License-Identifier: GPL-3.0-or-later
"""Clean, symmetric worker rest surface on the published paint topology.

Only authoring uses this module. Runtime animation skins a fixed mesh; it never
rebuilds an implicit field. Dimensions are model-space art controls.
"""

import math
import numpy as np
from limb_surface import LimbSurface

# A capsule midsection separates the upper and lower sockets without scaling limbs.
BODY_HALF_EXTENSION = 1.15
BODY_MIDSECTION = 1.4


def smooth_surface(positions, triangles, iterations=12):
    """Nonshrinking two-pass fairing on the welded graph, including socket rims."""
    adjacency = [set() for _ in positions]
    for triangle in triangles:
        for v in triangle:
            adjacency[v].update(int(k) for k in triangle if k != v)
    rows = np.array([v for v, ns in enumerate(adjacency) for _ in ns])
    cols = np.array([k for ns in adjacency for k in sorted(ns)])
    degree = np.bincount(rows)[:, None]
    positions = np.array(positions, dtype=float, copy=True)
    for _ in range(iterations):
        for rate in (0.5, -0.53):
            mean = np.zeros_like(positions)
            np.add.at(mean, rows, positions[cols])
            positions += rate * (mean / degree - positions)
    return positions


def vertex_normals(positions, triangles):
    face = np.cross(
        positions[triangles[:, 1]] - positions[triangles[:, 0]],
        positions[triangles[:, 2]] - positions[triangles[:, 0]],
    )
    normals = np.zeros_like(positions)
    for corner in triangles.T:
        np.add.at(normals, corner, face)
    lengths = np.linalg.norm(normals, axis=1)
    if np.any(lengths < 1e-10):
        raise ValueError("Degenerate worker surface normal")
    return normals / lengths[:, None]


def build_surface(definition, sculpt=True):
    surface = LimbSurface(definition)
    radius, end, bulb = 2.8, 5.6, 2.05
    positions = []
    directions = np.array([(0, 1, 1), (0, -1, 1), (0, 1, -1), (0, -1, -1)]) / math.sqrt(
        2
    )
    for descriptor in surface.vertices:
        kind = descriptor[0]
        if kind == "average":
            positions.append(np.mean([positions[i] for i in descriptor[1]], axis=0))
            continue
        if kind == "body":
            v = descriptor[1] / np.linalg.norm(descriptor[1])
            v = np.array(
                (v[0], (v[1] - v[2]) / math.sqrt(2), (v[1] + v[2]) / math.sqrt(2))
            )
            # Expand each socket on a disjoint angular patch of the torso.
            for direction in directions:
                cosine = float(np.clip(v @ direction, -1, 1))
                theta = math.acos(cosine)
                if 1e-8 < theta < math.pi / 4:
                    tangent = (v - direction * cosine) / math.sin(theta)
                    theta = math.pi / 4 * (theta / (math.pi / 4)) ** 0.41
                    v = direction * math.cos(theta) + tangent * math.sin(theta)
            positions.append(v * radius)
            continue
        branch = descriptor[1]
        direction = directions[branch]
        if kind == "tip":
            positions.append(direction * (end + bulb))
            continue
        front = np.array((1.0, 0.0, 0.0))
        side = np.cross(direction, front)
        angle = surface.branches[branch][3][descriptor[-1]]
        radial = front * math.cos(angle) + side * math.sin(angle)
        if kind == "cap":
            latitude = descriptor[2]
            positions.append(
                direction * (end + bulb * math.sin(latitude))
                + radial * bulb * math.cos(latitude)
            )
        else:
            s = descriptor[2]
            opening = math.pi / 4 * (math.atan(0.25) / (math.pi / 4)) ** 0.41
            socket = radius * math.cos(opening)
            distance = socket + (end - socket) * s
            # A cylindrical connector blends into the rounded terminal and collar.
            width = max(1.7, math.sqrt(max(0, bulb**2 - (end - distance) ** 2)))
            t = min(1, s / 0.15)
            t = t * t * (3 - 2 * t)
            width = (1 - t) * radius * math.sin(opening) + t * width
            positions.append(direction * distance + radial * width)
    positions = smooth_surface(positions, surface.triangles, 34)
    positions[:, 0] *= 0.8125
    # Extend the torso after fairing so shaft cross-sections and caps retain
    # their exact shape. Both ends translate with their attachment regions.
    for i, descriptor in enumerate(surface.vertices):
        if descriptor[0] == "body":
            positions[i, 2] += BODY_HALF_EXTENSION * np.clip(
                positions[i, 2] / BODY_MIDSECTION, -1, 1
            )
        elif descriptor[0] == "average":
            positions[i] = np.mean(positions[list(descriptor[1])], axis=0)
        else:
            positions[i, 2] += BODY_HALF_EXTENSION * (1 if descriptor[1] < 2 else -1)
    # A soft waist preserves both reflection symmetries as the worker rolls.
    # Limb vertices stay unchanged.
    if sculpt:
        for i, descriptor in enumerate(surface.vertices):
            if descriptor[0] == "body":
                x, y, z = positions[i]
                waist = math.exp(-((z / 1.7) ** 2))
                positions[i, 0] = x * (1 - 0.18 * waist)
                positions[i, 1] = y * (1 - 0.0 * waist)
            elif descriptor[0] == "average":
                positions[i] = np.mean(positions[list(descriptor[1])], axis=0)
    anchors = np.zeros((13, 3))
    for direction, path in zip(directions, definition["paths"]):
        for joint, distance in zip(path, (2.2, 3.7, end)):
            anchors[joint] = direction * distance
    return positions, vertex_normals(positions, surface.triangles), anchors


def skin_weights(positions, anchors, paths):
    """Share socket motion with the torso, keeping each shaft ring rigid.

    The three controls of a lobe have one orientation and differ only in axial
    translation. Equal weights around each ring preserve its cross-section;
    terminal caps follow their last control exclusively.
    """
    definition = {"paths": paths, "socketDivisions": 4, "ringsPerSegment": 3}
    surface = LimbSurface(definition)
    if len(positions) != len(surface.vertices):
        raise ValueError("Worker weights require the published surface topology")
    directions = np.array(
        [anchors[path[-1]] / np.linalg.norm(anchors[path[-1]]) for path in paths]
    )
    # Keep attachment support independent of the waist shaping.
    positions, _, _ = build_surface(definition, sculpt=False)
    dense_rows = []
    for point, descriptor in zip(positions, surface.vertices):
        dense = np.zeros(13)
        kind = descriptor[0]
        if kind == "average":
            dense = np.mean([dense_rows[i] for i in descriptor[1]], axis=0)
        elif kind == "body":
            point = point.copy()
            # Evaluate the socket support in the original short-body space.
            # Elongating the center must not let upper limbs pull the hips.
            point[2] = (
                point[2] / (1 + BODY_HALF_EXTENSION / BODY_MIDSECTION)
                if abs(point[2]) < BODY_MIDSECTION + BODY_HALF_EXTENSION
                else point[2] - np.sign(point[2]) * BODY_HALF_EXTENSION
            )
            direction = point / np.linalg.norm(point)
            t = np.clip(
                (directions @ direction - math.cos(1.5))
                / (math.cos(0.7) - math.cos(1.5)),
                0,
                1,
            )
            weights = 0.87385 * t * t * (3 - 2 * t)
            dense[1::3] = weights
            dense[0] = 1 - max(weights)
            dense /= dense.sum()
        else:
            branch = descriptor[1]
            t = descriptor[2] if kind == "ring" else 1
            dense[1 + 3 * branch] = max(0, 1 - 2 * t)
            dense[2 + 3 * branch] = 1 - abs(2 * t - 1)
            dense[3 + 3 * branch] = max(0, 2 * t - 1)
        dense_rows.append(dense)
    influences = []
    for dense in dense_rows:
        joints = np.argsort(-dense, kind="stable")[:4].tolist()
        if np.count_nonzero(dense > 1e-10) > 4 or np.any(dense < 0):
            raise ValueError("Worker weights exceed the four-influence contract")
        weights = dense[joints]
        weights /= weights.sum()
        influences.append((joints, weights.tolist()))
    return influences
