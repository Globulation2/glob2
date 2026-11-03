# SPDX-License-Identifier: GPL-3.0-or-later
"""Approved floating harvest motion in original worker component coordinates."""
import math
import numpy as np

# A continuous periodic cubic B-spline, rather than three eased stages.
# The rounded loop retains open-wide / gather-forward / retract semantics.
controls = np.array(
    [[-3.2, 1.9], [-1.7, 3.0], [-1.7, 6.1], [-4.6, 5.4], [-6.0, 2.0], [-5.1, 1.55]]
)


def hand_path(t):
    q = t - 0.065 * math.sin(2 * math.pi * t)
    u = (q % 1) * len(controls)
    i = int(u)
    f = u - i
    b = (
        np.array(
            [
                (1 - f) ** 3,
                3 * f**3 - 6 * f * f + 4,
                -3 * f**3 + 3 * f * f + 3 * f + 1,
                f**3,
            ]
        )
        / 6
    )
    return b @ controls[[(i - 1) % 6, i % 6, (i + 1) % 6, (i + 2) % 6]]


def arm_target(t):
    w = 2 * math.pi * t
    hand = hand_path(t)
    shoulder = np.array(
        [-1.7 + 0.18 * math.sin(w - 0.85), 1.75 + 0.12 * math.sin(w - 1.1)]
    )
    delta = hand - shoulder
    r = np.linalg.norm(delta)
    direction = delta / r
    # Tiny compliance retains the glob's soft anatomy without a rigid hinge.
    a = 2.05 * (1 + 0.025 * math.sin(w - 0.3))
    b = 2.25 * (1 + 0.025 * math.sin(w - 0.65))
    assert abs(a - b) < r < a + b
    along = (a * a - b * b + r * r) / (2 * r)
    elbow = (
        shoulder
        + along * direction
        + math.sqrt(a * a - along * along) * np.array([direction[1], -direction[0]])
    )
    return shoulder, elbow, hand


def pose(t, original):
    m = original.copy()
    c = np.zeros((13, 3))
    w = 2 * math.pi * t
    c[0] = [
        0.017 + 0.28 * math.sin(w - 1.0),
        -0.079 + 0.075 * math.sin(w - 0.4),
        0.15 * math.sin(w - 0.65),
    ]
    for sign, ids, lag in [(1, [1, 2, 3], 0), (-1, [4, 5, 6], 0.045)]:
        q = t - lag
        shoulder, elbow, hand = arm_target(q)
        # Elbow follow-through is delayed relative to the leading hand.
        elbow = (
            0.55 * elbow
            + 0.30 * arm_target(q - 0.025)[1]
            + 0.15 * arm_target(q - 0.05)[1]
        )
        c[ids[0]] = [shoulder[0], sign * shoulder[1], 0]
        c[ids[1]] = [elbow[0], sign * elbow[1], 0]
        c[ids[2]] = [hand[0], sign * hand[1], 0]
    for sign, ids in [(-1, [7, 9, 11]), (1, [8, 10, 12])]:
        k = w - 1.15 + sign * 0.25
        c[ids[0]] = [2.15, sign * 1.15, 0]
        c[ids[1]] = [
            3.95 + 0.08 * math.sin(k - 0.3),
            sign * (1.9 + 0.14 * math.sin(k)),
            0.08 * math.cos(k),
        ]
        c[ids[2]] = [
            5.8 + 0.16 * math.sin(k - 0.6),
            sign * (2.45 + 0.30 * math.sin(k - 0.3)),
            0.15 * math.cos(k - 0.3),
        ]
    # The whole soft body yields slightly after the stroke, with its plane intact.
    angle = 0.028 * math.sin(w - 0.9)
    rotation = np.array(
        [
            [math.cos(angle), -math.sin(angle), 0],
            [math.sin(angle), math.cos(angle), 0],
            [0, 0, 1],
        ]
    )
    c[1:] = c[1:] @ rotation.T + c[0]
    m[:, :3, :3] = np.einsum("ij,pjk->pik", rotation, m[:, :3, :3])
    m[0, :3, :3] *= 1 + 0.025 * math.sin(w - 0.8)
    m[:, :3, 3] = c
    return m
