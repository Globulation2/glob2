#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fit GSR1 worker and warrior rigs to the baked metaball clips.

The installed GSK1 clips are per-frame fits of the published paint topology to
the original metaball field. This script keeps that look instead of authoring a
new surface: the rest mesh is the same surface evaluated at the source rig's rest
pose, bones start at the original metaball chain, skin weights are solved by
least squares against every baked frame of every clip, and the bone transforms
are then refined against the same frames (skinning decomposition). The runtime
only skins the fixed mesh; it never rebuilds an implicit field.

Run in Blender 3.6.23:
  blender --background --factory-startup -t 1 --python-exit-code 1 \
    --python tools/skins/fit_unit_rigs.py -- --model worker --output artifacts/rig/worker
"""

import argparse
import json
import math
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Matrix, Vector
from export_units import open_source, components, sample_pose, influence_matrices
from export_rig import NO_PARENT, encode, write_candidate
from limb_surface import LimbSurface
from rig_scene import (
    MAPPINGS,
    ROOT,
    baked_frames,
    check_action_source,
    clip_descriptor,
    clip_record,
    sample_of_frame,
    create_scene,
    import_action,
    install_action,
)

CLIPS = {"worker": ("walk", "swim", "harvest"), "warrior": ("walk", "swim", "fight")}
LIMBS = (("arm", "R"), ("arm", "L"), ("leg", "R"), ("leg", "L"))
# A clip keeps the fewest samples whose repeats across directions stay within
# this distance (model units) of each other; walk gaits repeat every two
# directions, the warrior's swim stroke never repeats within the eight.
PERIOD_TOLERANCE = 0.25
DEFAULT_ITERATIONS = 20
WEIGHT_ITERATIONS = 400
# Weight smoothness: pull each vertex toward its neighbours' mean weights with
# this much of the data term's strength. Abrupt weight changes between
# neighbours fold faces where limbs meet the torso.
DEFAULT_SMOOTHNESS = 0.0
SMOOTHING_ROUNDS = 3
# Neighbour-averaging passes over the dense weights before choosing each
# vertex's four influences, so adjacent vertices pick the same bones.
DEFAULT_SUPPORT_PASSES = 5
# How far (model units) refinement may move a bone from its metaball-chain
# position. Unbounded translations fix large placement errors but also pinch
# the socket rings into dark flipped specks.
DEFAULT_TRANSLATION_BOUND = 1.0
# Rest mesh per model: the worker's body ball is smaller than its shoulder and
# hip balls, so the surface at the source rest pose is an hourglass that
# skinning would carry into every frame; its rest comes from the baked frames
# un-posed through their bones. The warrior's body ball is the largest, its
# source rest surface is already right, and the un-posed mean cracks its joints.
REST_SOURCES = {"worker": "unposed", "warrior": "source"}
MAXIMUM_INFLUENCES = 4
# Refined bone scale relative to the bind pose; the format allows far more,
# this keeps a bone from "explaining" a merge by collapsing.
SCALE_BOUNDS = (0.5, 2.0)
DEFINITION_PATH = ROOT / "datasrc/gfx/authored/skins/limb-surfaces.json"


def body_basis(root, axes):
    """Body front/lateral/vertical axes exactly as LimbSurface.evaluate builds them."""
    rotation = root[:3, :3] / np.linalg.norm(root[:3, :3], axis=0)
    basis = np.column_stack([rotation[:, abs(i) - 1] * (1 if i > 0 else -1) for i in axes])
    if np.linalg.det(basis) <= 0:
        raise ValueError("Body axes must keep a right-handed basis")
    return basis


def sample_source(model, clip, axes):
    """All 256 component transforms with the heading undone, plus the body basis."""
    source, scene = open_source(model, clip)
    parts = components(scene)
    origin = float(scene.objects["RotEmpty"].rotation_euler.z)
    transforms = np.zeros((256, len(parts), 4, 4))
    for frame in range(256):
        direction, phase = divmod(frame, 32)
        sample_pose(scene, direction, phase, origin)
        undo = np.array(Matrix.Rotation(direction * math.pi / 4, 4, "Z"))
        transforms[frame] = undo @ influence_matrices(parts)
    bases = np.array([body_basis(t[0], axes) for t in transforms])
    field = (
        np.array([p.data.elements[0].radius for p in parts]),
        np.array([p.data.elements[0].stiffness for p in parts]),
        parts[0].data.threshold,
    )
    return source, scene, parts, origin, transforms, bases, field


def rest_transforms(scene, parts, origin):
    """Component transforms with every bone in its rest position."""
    armature = next(o for o in scene.objects if o.type == "ARMATURE")
    armature.data.pose_position = "REST"
    bpy.context.view_layer.update()
    sample_pose(scene, 0, 0, origin)
    rest = influence_matrices(parts)
    armature.data.pose_position = "POSE"
    bpy.context.view_layer.update()
    return rest


def bone_layout(paths, midpoints=True):
    """(name, parent, kind, limb, component) per bone, parent ordered.

    With ``midpoints`` every segment also gets a bone halfway to the next
    component, giving the merge regions between balls their own control; this
    lowers the fit error by roughly a fifth over the bare metaball chain.
    """
    layout = [("body", -1, "body", -1, 0)]
    for limb, path in enumerate(paths):
        limb_name, side = LIMBS[limb]
        layout.append((f"{limb_name}.socket.{side}", 0, "socket", limb, path[0]))
        parent = len(layout) - 1
        for k, part in enumerate(path):
            if midpoints:
                layout.append((f"{limb_name}.{k + 1}.mid.{side}", parent, "mid", limb, part))
                parent = len(layout) - 1
            layout.append((f"{limb_name}.{k + 1}.{side}", parent, "ball", limb, part))
            parent = len(layout) - 1
    return layout


def scale_of(transform):
    return float(np.linalg.norm(transform[:3, :3], axis=0).mean())


def chain_bones(layout, paths, transforms, basis, rest, rest_basis, unit_scale=False):
    """Model-space bone transforms built from component centres.

    The body follows the clip's body basis; each limb bone sits on its
    component with the minimal rotation taking the rest segment direction to
    the posed one. Socket bones sit at the body centre and turn with the limb's
    first segment so the attachment region can follow the limb.
    """
    result = []
    for _, _, kind, limb, part in layout:
        if kind == "body":
            rotation, centre = basis, transforms[0, :3, 3]
            scale = scale_of(transforms[0]) / scale_of(rest[0])
        else:
            path = paths[limb]
            k = path.index(part) if kind != "socket" else 0
            previous = 0 if k == 0 else path[k - 1]
            rest_direction = rest_basis.T @ (rest[part, :3, 3] - rest[previous, :3, 3])
            direction = basis.T @ (transforms[part, :3, 3] - transforms[previous, :3, 3])
            swing = Vector(rest_direction).rotation_difference(Vector(direction))
            rotation = basis @ np.array(swing.to_matrix())
            source = 0 if kind == "socket" else part
            centre = transforms[source, :3, 3]
            if kind == "mid":
                centre = (transforms[previous, :3, 3] + centre) / 2
            scale = scale_of(transforms[source]) / scale_of(rest[source])
        world = np.eye(4)
        world[:3, :3] = rotation * (1.0 if unit_scale else scale)
        world[:3, 3] = centre
        result.append(world)
    return np.array(result)


def predict(relative, homogeneous, weights):
    """Linear blend skinning: (samples, vertices, 3)."""
    return np.einsum("sbij,vj,vb->svi", relative[:, :, :3, :], homogeneous, weights, optimize=True)


def neighbour_graph(triangles, count):
    """(rows, cols, degree) of the welded vertex adjacency for neighbour means."""
    adjacency = [set() for _ in range(count)]
    for a, b, c in triangles:
        adjacency[a].update((b, c))
        adjacency[b].update((a, c))
        adjacency[c].update((a, b))
    rows = np.array([v for v, ns in enumerate(adjacency) for _ in ns])
    cols = np.array([k for ns in adjacency for k in sorted(ns)])
    return rows, cols, np.bincount(rows, minlength=count)[:, None]


def neighbour_mean(values, graph):
    rows, cols, degree = graph
    total = np.zeros_like(values)
    np.add.at(total, rows, values[cols])
    return total / degree


def solve_weights(
    relative,
    homogeneous,
    targets,
    mirrors,
    graph=None,
    smoothness=0.0,
    prior=None,
    support_passes=0,
    allowed=None,
):
    """Non-negative least squares per vertex with at most four mirrored influences.

    ``relative`` stacks every clip's samples; ``targets`` are the matching mean
    poses. The sum-to-one constraint is a penalty row; weights are normalised
    afterwards. Mirrored vertices share mirrored weights exactly. With a
    ``graph`` and ``smoothness``, each vertex is also pulled toward its
    neighbours' mean weights (starting from ``prior``) over a few rounds.
    """
    vertices, bones = len(homogeneous), relative.shape[1]
    rows = relative.shape[0] * 3

    def descend(normal, rhs, lipschitz, weights, mask, iterations):
        # Accelerated projected gradient on the normal equations.
        z, previous, momentum = weights, weights, 1.0
        for _ in range(iterations):
            gradient = np.einsum("vbc,vc->vb", normal, z) - rhs
            current = np.maximum(0, z - gradient / lipschitz[:, None]) * mask
            next_momentum = (1 + math.sqrt(1 + 4 * momentum * momentum)) / 2
            z = current + (momentum - 1) / next_momentum * (current - previous)
            previous, momentum = current, next_momentum
        return previous

    def solve(mask, prior):
        weights = np.zeros((vertices, bones))
        for start in range(0, vertices, 256):
            chunk = slice(start, min(vertices, start + 256))
            count = chunk.stop - chunk.start
            design = np.einsum(
                "sbij,vj->vsib", relative[:, :, :3, :], homogeneous[chunk], optimize=True
            ).reshape(count, rows, bones)
            observed = targets[:, chunk, :].transpose(1, 0, 2).reshape(count, rows)
            normal = np.einsum("vrb,vrc->vbc", design, design, optimize=True)
            rhs = np.einsum("vrb,vr->vb", design, observed, optimize=True)
            strength = np.mean(np.einsum("vbb->vb", normal), axis=1)
            normal += strength[:, None, None]
            rhs += strength[:, None]
            if prior is not None:
                pull = np.broadcast_to(smoothness, (vertices,))[chunk] * strength
                normal += pull[:, None, None] * np.eye(bones)
                rhs += pull[:, None] * prior[chunk]
            lipschitz = np.linalg.eigvalsh(normal).max(axis=1)
            initial = mask[chunk] / np.maximum(mask[chunk].sum(axis=1, keepdims=True), 1)
            weights[chunk] = descend(
                normal, rhs, lipschitz, initial, mask[chunk], WEIGHT_ITERATIONS
            )
        return weights

    def symmetric(weights):
        total = weights.copy()
        for vertex_map, bone_map in mirrors:
            total += weights[vertex_map][:, bone_map]
        return total / (1 + len(mirrors))

    def support(weights):
        # The four largest influences, kept identical across mirrored vertices
        # so a tie at the fourth place cannot break the weight symmetry.
        keep = np.argsort(-weights, axis=1, kind="stable")[:, :MAXIMUM_INFLUENCES]
        mask = np.zeros_like(weights)
        np.put_along_axis(mask, keep, 1, axis=1)
        for vertex_map, bone_map in mirrors:
            mask = np.minimum(mask, mask[vertex_map][:, bone_map])
        if np.any(mask.sum(axis=1) == 0):
            raise ValueError("Mirrored vertices share no influence")
        return mask

    rounds = SMOOTHING_ROUNDS if graph is not None and np.any(np.asarray(smoothness) > 0) else 1
    full = np.ones((vertices, bones)) if allowed is None else allowed
    dense = prior
    for _ in range(rounds):
        dense = symmetric(solve(full, None if dense is None else neighbour_mean(dense, graph)))
    chosen = dense
    for _ in range(support_passes if graph is not None else 0):
        chosen = neighbour_mean(chosen, graph)
    mask = support(chosen) * full
    restricted = dense
    for _ in range(rounds):
        restricted = symmetric(solve(mask, neighbour_mean(restricted, graph) if rounds > 1 else None)) * mask
    total = restricted.sum(axis=1, keepdims=True)
    if np.any(total <= 0):
        raise ValueError("A vertex lost every influence")
    return restricted / total


def refine_bones(relative, homogeneous, targets, weights, chain, bound):
    """One skinning-decomposition pass over every bone of one clip.

    For each bone, the residual the other bones leave at its vertices is fitted
    per sample by a translation and uniform scale (the position half of Le &
    Deng's SSDR bone update). Rotations stay on the metaball chain: the runtime
    rotates rest normals with the blended bones instead of recomputing them,
    and freely refined rotations, which positions alone leave ambiguous on
    round regions, turned those normals away from the surface and shaded dark
    crescents at the sockets.
    """
    relative = relative.copy()
    positions = homogeneous[:, :3]
    current = predict(relative, homogeneous, weights)
    for bone in range(relative.shape[1]):
        selected = np.nonzero(weights[:, bone] > 1e-6)[0]
        if len(selected) < 4:
            continue
        w = weights[selected, bone]
        squared = w * w
        p = positions[selected]
        before = np.einsum("sij,vj->svi", relative[:, bone, :3, :], homogeneous[selected])
        residual = targets[:, selected, :] - (current[:, selected, :] - w[None, :, None] * before)
        q = residual / w[None, :, None]
        centre_p = squared @ p / squared.sum()
        x = p - centre_p
        centre_q = np.einsum("v,svi->si", squared, q) / squared.sum()
        y = q - centre_q[:, None, :]
        spread = (squared * (x * x).sum(axis=1)).sum()
        rotation = relative[:, bone, :3, :3] / np.linalg.norm(relative[:, bone, :3, 0], axis=1)[:, None, None]
        rotated = np.einsum("sij,vj->svi", rotation, x)
        scale = np.clip(np.einsum("v,svi,svi->s", squared, rotated, y) / spread, *SCALE_BOUNDS)
        relative[:, bone, :3, :3] = rotation * scale[:, None, None]
        translation = centre_q - np.einsum("sij,j->si", relative[:, bone, :3, :3], centre_p)
        delta = translation - chain[:, bone, :3, 3]
        length = np.linalg.norm(delta, axis=1)
        delta *= np.minimum(1.0, bound / np.maximum(length, 1e-12))[:, None]
        relative[:, bone, :3, 3] = chain[:, bone, :3, 3] + delta
        after = np.einsum("sij,vj->svi", relative[:, bone, :3, :], homogeneous[selected])
        current[:, selected, :] += w[None, :, None] * (after - before)
    return relative


def allowed_influences(surface, layout, paths, torso_sockets=True):
    """Which bones may influence each vertex: the torso uses the body and socket
    bones (or the body alone), a limb uses the body, its socket and its own
    bones. Quad centres take the union of their corners. This keeps a torso
    vertex from borrowing a limb bone whose rotation would turn its normal away
    from the surface."""
    limb_of = np.array([limb for _, _, _, limb, _ in layout])
    kind_of = [kind for _, _, kind, _, _ in layout]
    torso = np.array(
        [kind == "body" or (torso_sockets and kind == "socket") for kind in kind_of], dtype=float
    )
    allowed = np.zeros((len(surface.vertices), len(layout)))
    for v, descriptor in enumerate(surface.vertices):
        if descriptor[0] == "average":
            allowed[v] = np.max(allowed[list(descriptor[1])], axis=0)
        elif descriptor[0] == "body":
            allowed[v] = torso
        else:
            limb = descriptor[1]
            allowed[v] = (limb_of == limb) | (np.array(kind_of) == "body")
    return allowed


def mirror_maps(layout, paths, bind, rest_basis, centre):
    """Bone partners under the surface chart's front/back and top/bottom reflections."""
    local = np.array([rest_basis.T @ (b[:3, 3] - centre) for b in bind])
    first = {limb: rest_basis.T @ (bind[i][:3, 3] - centre)
             for i, (_, _, kind, limb, _) in enumerate(layout) if kind == "ball" and paths[limb][0] == layout[i][4]}
    maps = {}
    for axis, name in ((0, "frontBack"), (2, "topBottom")):
        limb_partner = {}
        for limb, direction in first.items():
            mirrored = direction.copy()
            mirrored[axis] *= -1
            limb_partner[limb] = min(first, key=lambda other: np.linalg.norm(first[other] - mirrored))
        partner = []
        for i, (_, _, kind, limb, part) in enumerate(layout):
            if kind == "body":
                partner.append(i)
                continue
            other_limb = limb_partner[limb]
            k = paths[limb].index(part) if kind != "socket" else -1
            partner.append(
                next(
                    j
                    for j, (_, _, other_kind, candidate, other_part) in enumerate(layout)
                    if candidate == other_limb
                    and other_kind == kind
                    and (kind == "socket" or paths[candidate].index(other_part) == k)
                )
            )
        partner = np.array(partner)
        mirrored = local.copy()
        mirrored[:, axis] *= -1
        if np.abs(local[partner] - mirrored).max() > 1e-3:
            raise ValueError(f"Rest skeleton is not {name} symmetric")
        maps[name] = partner
    return maps


def face_normals(x, triangles):
    return np.cross(x[:, triangles[:, 1]] - x[:, triangles[:, 0]], x[:, triangles[:, 2]] - x[:, triangles[:, 0]])


def flipped_faces(posed, actual, triangles):
    """Faces whose posed normal opposes the baked one, per frame."""
    return ((face_normals(posed, triangles) * face_normals(actual, triangles)).sum(axis=2) < 0).sum(axis=1)


def normal_disagreement(relative, normals, weights, posed, triangles):
    """Vertices per frame whose rig-rotated normal strays from the posed surface.

    The runtime rotates rest normals with the blended bones instead of
    recomputing them, so this is what shading actually sees.
    """
    rotation = relative[:, :, :3, :3] / np.linalg.norm(relative[:, :, :3, 0], axis=2)[:, :, None, None]
    rotated = np.einsum("sbij,vj,vb->svi", rotation, normals, weights, optimize=True)
    rotated /= np.maximum(np.linalg.norm(rotated, axis=2), 1e-12)[:, :, None]
    faces = face_normals(posed, triangles)
    geometric = np.zeros_like(posed)
    for corner in range(3):
        np.add.at(geometric, (slice(None), triangles[:, corner]), faces)
    geometric /= np.maximum(np.linalg.norm(geometric, axis=2), 1e-12)[:, :, None]
    return ((rotated * geometric).sum(axis=2) < 0.7).sum(axis=1)


def fit(
    model,
    iterations=DEFAULT_ITERATIONS,
    midpoints=True,
    smoothness=DEFAULT_SMOOTHNESS,
    support_passes=DEFAULT_SUPPORT_PASSES,
    translation_bound=DEFAULT_TRANSLATION_BOUND,
    torso_smoothness=0.0,
    torso_sockets=True,
    rest_source=None,
    log=print,
):
    definition = json.loads(DEFINITION_PATH.read_text())[model]
    paths = definition["paths"]
    surface = LimbSurface(definition)
    clips = CLIPS[model]
    kinds = np.array([vertex[0] for vertex in surface.vertices])
    sources, assets, views, targets, actual, sampled, samples = [], [], [], {}, {}, {}, {}
    rest = rest_basis = None
    for clip in clips:
        axes = definition.get("clipBodyAxes", {}).get(clip, definition["bodyAxes"])
        source, scene, parts, origin, transforms, bases, field = sample_source(model, clip, axes)
        sources.append(source)
        if clip == clips[0]:
            rest = rest_transforms(scene, parts, origin)
            rest_basis = body_basis(rest[0], definition["bodyAxes"])
            radii, stiffness, threshold = field
        asset, uv, indices, positions, size, view = baked_frames(model, clip)
        if not np.array_equal(surface.triangles, indices) or not np.array_equal(
            surface.uv.astype("<f4"), uv
        ):
            raise ValueError(f"{model} paint topology changed")
        assets += [asset, asset.with_suffix(".view.json")]
        views.append(view)
        actual[clip] = positions
        for count in sorted(MAPPINGS):
            groups = positions.reshape(256 // count, count, -1, 3)
            if np.abs(groups - groups.mean(axis=0)).max() <= PERIOD_TOLERANCE:
                break
        samples[clip] = count
        targets[clip] = groups.mean(axis=0)
        sampled[clip] = (transforms, bases)
    layout = bone_layout(paths, midpoints)
    bind = chain_bones(layout, paths, rest, rest_basis, rest, rest_basis, unit_scale=True)
    inverse_bind = np.linalg.inv(bind)
    centre = rest[0, :3, 3]
    reflections = surface.contract()["reflections"]

    def symmetrised(positions, tolerance):
        """Average the surface with its reflections in the body frame."""
        local = (positions - centre) @ rest_basis
        images = [local]
        for axis, name in ((0, "frontBack"), (2, "topBottom")):
            mirrored = local[reflections[name]].copy()
            mirrored[:, axis] *= -1
            images.append(mirrored)
        both = local[np.array(reflections["frontBack"])[reflections["topBottom"]]].copy()
        both[:, [0, 2]] *= -1
        images.append(both)
        asymmetry = float(max(np.abs(image - local).max() for image in images))
        if asymmetry > tolerance:
            raise ValueError(f"Rest surface is not symmetric: {asymmetry}")
        return centre + np.mean(images, axis=0) @ rest_basis.T, asymmetry

    # The source rest pose gives exact, symmetric bones; the surface there is
    # only a starting point.
    rest_positions, asymmetry = symmetrised(
        surface.evaluate(rest, radii, stiffness, threshold, clips[0]), 1e-3
    )
    bone_mirrors = mirror_maps(layout, paths, bind, rest_basis, centre)
    front_back = np.array(reflections["frontBack"])
    top_bottom = np.array(reflections["topBottom"])
    mirrors = [
        (front_back, bone_mirrors["frontBack"]),
        (top_bottom, bone_mirrors["topBottom"]),
        (front_back[top_bottom], bone_mirrors["frontBack"][bone_mirrors["topBottom"]]),
    ]
    relative = {}
    for clip in clips:
        transforms, bases = sampled[clip]
        world = np.array(
            [
                chain_bones(layout, paths, transforms[s], bases[s], rest, rest_basis)
                for s in range(samples[clip])
            ]
        )
        relative[clip] = world @ inverse_bind
    if (rest_source or REST_SOURCES[model]) == "unposed":
        # Each vertex's mean position over every baked frame, un-posed through
        # its home bone (the body, or its limb's nearest chain bone); see
        # REST_SOURCES for why the worker needs this.
        home = np.zeros(len(rest_positions), dtype=int)
        bone_centres = bind[:, :3, 3]
        for v, descriptor in enumerate(surface.vertices):
            if descriptor[0] in ("body", "average"):
                continue
            candidates = [
                i for i, (_, _, kind, limb, _) in enumerate(layout)
                if limb == descriptor[1] and kind in ("ball", "mid")
            ]
            home[v] = candidates[
                int(np.argmin(np.linalg.norm(bone_centres[candidates] - rest_positions[v], axis=1)))
            ]
        for v, descriptor in enumerate(surface.vertices):
            if descriptor[0] == "average":
                corners = home[list(descriptor[1])]
                home[v] = np.bincount(corners).argmax()
        unposed = np.zeros_like(rest_positions)
        count = 0
        for clip in clips:
            inverse = np.linalg.inv(relative[clip][:, :, :, :])
            for s in range(samples[clip]):
                hom = np.concatenate((targets[clip][s], np.ones((len(unposed), 1))), axis=1)
                unposed += np.einsum("vij,vj->vi", inverse[s][home][:, :3, :], hom)
                count += 1
        rest_positions, asymmetry = symmetrised(unposed / count, np.inf)
    normals = surface.normals(rest_positions)
    homogeneous = np.concatenate((rest_positions, np.ones((len(rest_positions), 1))), axis=1)
    stacked = lambda: np.concatenate([relative[c] for c in clips])
    mean_targets = np.concatenate([targets[c] for c in clips])
    graph = neighbour_graph(surface.triangles, len(homogeneous))
    allowed = allowed_influences(surface, layout, paths, torso_sockets)
    torso = np.array([descriptor[0] == "body" for descriptor in surface.vertices])
    for v, descriptor in enumerate(surface.vertices):
        if descriptor[0] == "average":
            torso[v] = all(torso[list(descriptor[1])])
    pull = np.where(torso, max(smoothness, torso_smoothness), smoothness)
    solve = lambda prior: solve_weights(
        stacked(), homogeneous, mean_targets, mirrors, graph, pull, prior, support_passes, allowed
    )

    def error(weights):
        return math.sqrt(
            np.mean(np.sum((predict(stacked(), homogeneous, weights) - mean_targets) ** 2, axis=2))
        )

    chain = {clip: relative[clip].copy() for clip in clips}
    started = time.time()
    weights = solve(None)
    history = [error(weights)]
    log(
        f"{model}: {len(layout)} bones, samples {samples}, initial rms {history[-1]:.4f} "
        f"({time.time() - started:.0f}s)"
    )
    for iteration in range(iterations):
        for clip in clips:
            relative[clip] = refine_bones(
                relative[clip], homogeneous, targets[clip], weights, chain[clip], translation_bound
            )
        weights = solve(weights)
        history.append(error(weights))
        log(f"{model}: pass {iteration + 1} rms {history[-1]:.4f} ({time.time() - started:.0f}s)")
        if history[-1] > history[-2] * 0.995:
            break
    influences = []
    for row in weights:
        joints = np.argsort(-row, kind="stable")[:MAXIMUM_INFLUENCES].tolist()
        values = row[joints]
        values[values < 1e-10] = 0
        influences.append((joints, (values / values.sum()).tolist()))
    reports = {}
    for clip in clips:
        mapping = sample_of_frame(samples[clip])
        posed = predict(relative[clip], homogeneous, weights)[mapping]
        distance = np.linalg.norm(posed - actual[clip], axis=2)
        per_frame = distance.mean(axis=1)
        flipped = flipped_faces(posed, actual[clip], surface.triangles)
        strayed = normal_disagreement(
            relative[clip][mapping], normals, weights, posed, surface.triangles
        )
        reports[clip] = {
            "flippedFacesMax": int(flipped.max()),
            "flippedFacesMean": float(flipped.mean()),
            "normalsStrayedMax": int(strayed.max()),
            "normalsStrayedMean": float(strayed.mean()),
            "rms": float(np.sqrt(np.mean(distance**2))),
            "p95": float(np.quantile(distance, 0.95)),
            "max": float(distance.max()),
            "worstFrame": int(per_frame.argmax()),
            "worstFrameMean": float(per_frame.max()),
            "kinds": {
                kind: {
                    "rms": float(np.sqrt(np.mean(distance[:, kinds == kind] ** 2))),
                    "max": float(distance[:, kinds == kind].max()),
                }
                for kind in ("body", "ring", "cap", "tip", "average")
            },
        }
    extent = float(np.abs(rest_positions - rest_positions.mean(axis=0)).max())
    return {
        "surface": surface,
        "layout": layout,
        "bind": bind,
        "positions": rest_positions,
        "normals": normals,
        "uv": surface.uv,
        "indices": surface.triangles,
        "influences": influences,
        "relative": relative,
        "samples": samples,
        "views": dict(zip(clips, views)),
        "sources": sources,
        "assets": assets,
        "reports": reports,
        "history": history,
        "extent": extent,
        "asymmetry": asymmetry,
        "size": baked_frames(model, clips[0])[4],
    }


def local_tracks(layout, world):
    """Parent-relative sample transforms as mathutils matrices."""
    tracks = []
    for sample in world:
        tracks.append(
            [
                Matrix(sample[i] if parent < 0 else np.linalg.inv(sample[parent]) @ sample[i])
                for i, (_, parent, *_) in enumerate(layout)
            ]
        )
    return tracks


def author(
    model,
    output,
    iterations,
    action_blend=None,
    action_name=None,
    preview_clip=None,
    result=None,
    midpoints=True,
    smoothness=DEFAULT_SMOOTHNESS,
    support_passes=DEFAULT_SUPPORT_PASSES,
    translation_bound=DEFAULT_TRANSLATION_BOUND,
    torso_smoothness=0.0,
    torso_sockets=True,
    rest_source=None,
):
    if bpy.app.version[:3] != (3, 6, 23):
        raise ValueError("Use Blender 3.6.23")
    if action_blend:
        action_blend = check_action_source(action_blend, output, model)
        if not action_name or preview_clip not in CLIPS[model]:
            raise ValueError("Action import requires --action and a valid --clip for this model")
    elif action_name or preview_clip:
        raise ValueError("--action and --clip require --action-blend")
    result = result or fit(
        model,
        iterations,
        midpoints,
        smoothness,
        support_passes,
        translation_bound,
        torso_smoothness,
        torso_sockets,
        rest_source,
    )
    layout, bind = result["layout"], result["bind"]
    scene_bones = [(name, parent, Matrix(bind[i])) for i, (name, parent, *_) in enumerate(layout)]
    scene, rig, rest = create_scene(
        model, result["positions"], result["uv"], result["indices"], result["influences"], scene_bones
    )
    encoded_bones = [
        (
            parent if parent >= 0 else NO_PARENT,
            Matrix(bind[i] if parent < 0 else np.linalg.inv(bind[parent]) @ bind[i]),
            Matrix(np.linalg.inv(bind[i])),
        )
        for i, (_, parent, *_) in enumerate(layout)
    ]
    vertices = [
        (result["positions"][v], result["normals"][v], result["uv"][v], *result["influences"][v])
        for v in range(len(result["positions"]))
    ]
    dependencies = [
        Path(__file__),
        Path(__file__).with_name("rig_scene.py"),
        Path(__file__).with_name("export_rig.py"),
        Path(__file__).with_name("export_units.py"),
        Path(__file__).with_name("limb_surface.py"),
        DEFINITION_PATH,
        ROOT / f"data/skins/colony-v1/{model}-surface.json",
    ] + result["sources"] + result["assets"]
    actions = []
    output.mkdir(parents=True, exist_ok=True)
    for clip_id, clip in enumerate(CLIPS[model]):
        if action_blend:
            if clip != preview_clip:
                continue
            scene.frame_end = result["samples"][clip]
            action, tracks = import_action(
                scene, rig, scene_bones, action_blend, action_name, result["samples"][clip]
            )
            actions.append(action)
            extra = [action_blend]
        else:
            world = result["relative"][clip] @ bind
            tracks = local_tracks(layout, world)
            actions.append(install_action(rig, rest, scene_bones, tracks, clip.title()))
            extra = []
        data = encode(
            vertices,
            result["indices"].ravel().tolist(),
            encoded_bones,
            [clip_record(clip_id, result["views"][clip], tracks)],
            result["size"],
        )
        write_candidate(
            output,
            f"{model}-{clip}",
            data,
            dependencies + extra,
            [{"id": clip_id, "name": action_name if action_blend else clip, **clip_descriptor(len(tracks))}],
        )
        report = {
            "model": model,
            "clip": clip,
            "bones": len(layout),
            "samples": len(tracks),
            "boneNames": [name for name, *_ in layout],
            "iterations": len(result["history"]) - 1,
            "meanRmsHistory": result["history"],
            "restExtent": result["extent"],
            "restAsymmetry": result["asymmetry"],
            "frames": result["reports"][clip],
        }
        (output / f"{model}-{clip}-fit.json").write_text(json.dumps(report, indent=2) + "\n")
        frames = result["reports"][clip]
        print(
            f"{model}-{clip}: {len(vertices)} vertices, {len(layout)} bones, {len(data)} bytes, "
            f"rms {frames['rms']:.3f} p95 {frames['p95']:.3f} max {frames['max']:.3f} "
            f"flipped faces max {frames['flippedFacesMax']} strayed normals max {frames['normalsStrayedMax']}"
        )
    rig.animation_data.action = actions[0]
    scene.frame_end = max(result["samples"].values())
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str((output / (model + ".blend")).resolve()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=CLIPS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--iterations", type=int, default=DEFAULT_ITERATIONS, help="bone refinement passes"
    )
    parser.add_argument(
        "--smoothness", type=float, default=DEFAULT_SMOOTHNESS, help="weight smoothness strength"
    )
    parser.add_argument(
        "--support-passes", type=int, default=DEFAULT_SUPPORT_PASSES, help="influence choice smoothing"
    )
    parser.add_argument(
        "--translation-bound",
        type=float,
        default=DEFAULT_TRANSLATION_BOUND,
        help="maximum refined bone displacement from the metaball chain",
    )
    parser.add_argument("--torso-smoothness", type=float, default=0.0, help="torso-only weight prior")
    parser.add_argument(
        "--rest",
        choices=("unposed", "source"),
        help="rest mesh: baked frames un-posed through their bones, or the source rest pose "
        "(default per model, see REST_SOURCES)",
    )
    parser.add_argument("--rigid-torso", action="store_true", help="torso follows the body bone only")
    parser.add_argument(
        "--no-midpoint-bones",
        action="store_true",
        help="use only the metaball chain bones, without segment midpoints",
    )
    parser.add_argument("--action-blend", type=Path, help="Import a 64-frame quaternion action")
    parser.add_argument("--action", help="Action to import")
    parser.add_argument("--clip", help="Gameplay clip slot the imported action previews")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    author(
        args.model,
        args.output,
        args.iterations,
        args.action_blend,
        args.action,
        args.clip,
        midpoints=not args.no_midpoint_bones,
        smoothness=args.smoothness,
        support_passes=args.support_passes,
        translation_bound=args.translation_bound,
        torso_smoothness=args.torso_smoothness,
        torso_sockets=not args.rigid_torso,
        rest_source=args.rest,
    )
