// SPDX-License-Identifier: GPL-3.0-or-later
// Compile with tsc --strict --noEmit to verify authoring declarations against
// the native-handle contracts. The expect-error cases must remain rejected.
import type { GeneratorContext } from "../data/generators/toolkit";

declare const c: GeneratorContext;
const t = c.toolkit.Grid.Torus(2, 2);
const terrain = c.mask(4, 2);
c.toolkit.Sketch.layBeaches(terrain, t);
c.toolkit.Sketch.writeVertices([2, 2, 2, 2]);
c.toolkit.Sketch.sprinkleSand(terrain, t, [1, 1, 1, 1], 0.1, 2, () => 0);
const integers = c.integers(4);
const fieldBuffer = c.field(4);
// Const vector arguments convert scalar widths; mutable vectors preserve storage.
c.toolkit.Sketch.writeVertices(integers);
c.toolkit.Sketch.writeVertices(fieldBuffer);
c.toolkit.Sketch.growWater(t, terrain, 0, 1, () => true, () => 0, integers, 1);
const blueprint = c.toolkit.Blueprints.fortsDesign();
c.toolkit.Sketch.layBeaches(blueprint.terrain, blueprint.t);
c.toolkit.Sketch.growWater(blueprint.t, blueprint.wall, 0, 1, () => true, () => 0, blueprint.homeOf, 1);

const objective = c.toolkit.Solve.Objective();
objective.add("variation", 1, 0.5);
const firstTerm: number = objective.terms().get(0).weight;
const same: boolean = c.toolkit.Drawing.SubtilePoint({ x: 1, y: 2 }).equals({ x: 1, y: 2 });
const pi: number = c.toolkit.Geometry.kPi;
const frontage = c.toolkit.Resources.resourceFrontages(c.toolkit.Grid.Flood(), 10);
const resourceKeys = frontage.keys();
const resource = frontage.get(resourceKeys.get(0));
if (resource) {
    const edges: number = resource.edges;
}

const field = c.toolkit.FertilityField.Field();
field.rebuild(2, 2, [1, 0, 0, 0], [0, 0, 0, 0], c.toolkit.FertilityField.Path.Adaptive);
const value: number = field.values().get(0);
field.values().clone().set(0, value);

// @ts-expect-error Mutable terrain requires a native buffer, so changes reach the caller.
c.toolkit.Sketch.layBeaches([2, 2, 2, 2], t);
// @ts-expect-error Fertility returns a const native vector; clone it before writing.
field.values().set(0, 1);
// @ts-expect-error Both mutable water and mutable scratch parameters require handles.
c.toolkit.Sketch.growWater(t, [0, 0, 0, 0], 0, 1, () => true, () => 0, [0, 0, 0, 0], 1);
// @ts-expect-error int32 storage cannot replace a mutable byte terrain buffer.
c.toolkit.Sketch.layBeaches(integers, t);
// @ts-expect-error double storage cannot replace a mutable byte terrain buffer.
c.toolkit.Sketch.layBeaches(fieldBuffer, t);
// @ts-expect-error byte masks cannot replace the mutable int32 scratch buffer.
c.toolkit.Sketch.growWater(t, terrain, 0, 1, () => true, () => 0, terrain, 1);
