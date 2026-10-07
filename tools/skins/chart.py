#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stable paint chart for folded glob surfaces (layout v2).

Installed workers and warriors use the retained authored chart, so rebuilding
their geometry never reinterprets a saved skin. The conformal unwrapper below
remains available for authoring new topology.

The v1 chart projected the whole body onto one plane, so surfaces facing
sideways shared a handful of texels while the front compressed many. This chart
unwraps one quarter of the rest surface (the quarter with non-negative virtual
depth and height: a quarter of the torso shell and half of each attached limb,
cut lengthwise along the fold and cut from the torso at its socket rim) using
Blender's angle-based unwrapper, one island per piece, and gives every vertex
the coordinates of its quarter representative. Front/back and top/bottom
counterparts therefore still sample identical texels. The only seams are the
socket rims, whose limb-side copies are welded to the torso vertices.
"""

import json
from pathlib import Path

import bpy
import numpy as np

MARGIN = 0.02


def quarter(surface):
    """Vertices of the representative quarter and the map from any vertex to it."""
    virtual = np.array(surface.virtual)
    reflections = surface.contract()["reflections"]
    front_back, top_bottom = np.array(reflections["frontBack"]), np.array(reflections["topBottom"])
    representative = np.arange(len(virtual))
    for v in range(len(virtual)):
        r = v
        if virtual[r, 0] < -1e-9:
            r = front_back[r]
        if virtual[r, 2] < -1e-9:
            r = top_bottom[r]
        representative[v] = r
    members = np.unique(representative)
    inside = np.zeros(len(virtual), dtype=bool)
    inside[members] = True
    triangles = [t for t in surface.triangles.tolist() if all(inside[v] for v in t)]
    if len(triangles) * 4 != len(surface.triangles):
        raise ValueError("Surface triangles do not split into four reflected quarters")
    return members, representative, triangles


def unwrap(positions, members, triangles):
    """Angle-based unwrap of the quarter mesh; returns UVs per member vertex."""
    local = {int(v): i for i, v in enumerate(members)}
    mesh = bpy.data.meshes.new("ChartQuarter")
    mesh.from_pydata(
        [positions[v].tolist() for v in members], [], [[local[v] for v in t] for t in triangles]
    )
    mesh.update()
    if mesh.validate(verbose=False):
        raise ValueError("Quarter mesh is not valid")
    obj = bpy.data.objects.new("ChartQuarter", mesh)
    bpy.context.scene.collection.objects.link(obj)
    try:
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        mesh.uv_layers.new(name="Chart")
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.unwrap(method="ANGLE_BASED", margin=0.03)
        bpy.ops.object.mode_set(mode="OBJECT")
        uv = np.full((len(members), 2), np.nan)
        layer = mesh.uv_layers.active.data
        for polygon in mesh.polygons:
            for loop_index in polygon.loop_indices:
                v = mesh.loops[loop_index].vertex_index
                coordinate = np.array(layer[loop_index].uv)
                if np.isnan(uv[v, 0]):
                    uv[v] = coordinate
                elif np.abs(uv[v] - coordinate).max() > 1e-6:
                    raise ValueError("Unwrap split a vertex across islands")
        if np.isnan(uv).any():
            raise ValueError("Unwrap left a vertex without coordinates")
    finally:
        bpy.context.scene.collection.objects.unlink(obj)
        bpy.data.objects.remove(obj)
        bpy.data.meshes.remove(mesh)
    return uv


def fitted(uv):
    """Fit the packed islands into the quadrant with a bleed margin."""
    low, high = uv.min(axis=0), uv.max(axis=0)
    scale = (1 - 2 * MARGIN) / max(high - low)
    result = (uv - low) * scale + MARGIN
    result += (1 - result.max(axis=0) - result.min(axis=0)) / 2
    return np.clip(result, 0, 1)


def chart(surface, rest_positions):
    """Per-vertex UVs for the whole surface, shared across its reflections."""
    if "paintChart" in surface.definition:
        source = surface.definition["paintChart"]
        charts = json.loads((Path(__file__).resolve().parents[2] / source["file"]).read_text())
        uv = np.array(charts[source["model"]], dtype=float)
        if uv.shape != (len(surface.vertices), 2) or not np.isfinite(uv).all() or np.any((uv < 0) | (uv > 1)):
            raise ValueError("Authored paint chart differs from the established topology")
        return uv
    return detail_chart(surface, rest_positions)


def detail_chart(surface, rest_positions):
    """Clean procedural unwrap, independent of the retained paint atlas."""
    members, representative, triangles = quarter(surface)
    uv = fitted(unwrap(np.asarray(rest_positions, dtype=float), members, triangles))
    lookup = {int(v): uv[i] for i, v in enumerate(members)}
    result = np.array([lookup[int(representative[v])] for v in range(len(representative))])
    return np.round(result, 6)
