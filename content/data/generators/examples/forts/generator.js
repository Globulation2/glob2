// SPDX-License-Identifier: GPL-3.0-or-later
// The native designer supplies geometry; JavaScript owns the generation stages.
const WOOD = 0, WHEAT = 1, STONE = 3, CHERRY = 5;
export function validateRequest(c) { return c.toolkit.Blueprints.fortsDesign().failure; }
export function generate(c) {
    const k = c.toolkit, o = c.request.options;
    c.stage("forts layout");
    const L = k.Blueprints.fortsDesign();
    if (L.failure) return L.failure;
    const t = L.t, wall = L.wall, plot = L.plot, homeOf = L.homeOf;
    const roads = L.roads, buffer = L.buffer, towns = L.towns;
    const homes = L.homes, villages = L.villages, d = L.design;
    const xy = i => [i % t.w, Math.trunc(i / t.w)];
    const clear = i => k.Planting.clearGround(...xy(i));
    const scaled = (n, percent) => k.Resources.scaledCount(Math.trunc(n), percent);
    c.stage("forts terrain");
    k.Sketch.writeVertices(L.terrain);
    if (k.Walls.designedStone(t, wall).gaps) return "The fort ramparts have a terrain gap.";
    for (let i = 0; i < t.size(); i++) if (wall.get(i)) c.setResource(i, STONE, 1);
    c.addTeams();
    const plan = k.Blueprints.fortsPlan(d.layout, o["home-size"]);
    c.stage("forts colonies");
    if (!k.Pipeline.settleColonies("forts-starts", team => {
        const mask = k.Pipeline.homeGrassMask(t, homeOf, team);
        for (let i = 0; i < t.size(); i++) if (plot.get(i) >= 0 || wall.get(i)) mask.set(i, 0);
        return mask;
    }, team => {
        const home = homes.get(team);
        return k.LegacyRegions.MapGeneratorPoint(Math.trunc(home.x) + d.x(plan.settleU, plan.settleV),
            Math.trunc(home.y) + d.y(plan.settleU, plan.settleV));
    })) return "Could not place fort colonies";
    c.stage("forts farms and countryside");
    const reserved = k.Planting.swarmSurroundings(t);
    const fertility = k.Growth.cropGrowthField(L.terrain, t);
    const growth = i => fertility.at(...xy(i));
    for (let p = 0; p < 2 * c.request.teams; p++) {
        const tiles = [];
        for (let i = 0; i < t.size(); i++)
            if (plot.get(i) === p && !reserved.get(i) && clear(i) && growth(i) > 0) tiles.push(i);
        tiles.sort((a, b) => growth(b) - growth(a));
        const wanted = 32 + scaled(p % 2 ? 16 : 32, o[p % 2 ? "wood-amount" : "wheat-amount"]);
        const placed = Math.min(wanted, tiles.length);
        let yieldSum = 0;
        for (let j = 0; j < placed; j++) { c.setResource(tiles[j], p % 2 ? WOOD : WHEAT, 1); yieldSum += growth(tiles[j]); }
        c.measure("forts.plot.target-tiles", wanted, p);
        c.measure("forts.plot.planted-tiles", placed, p);
        c.measure("forts.plot.planted-growth-sum", yieldSum, p);
        if (placed < wanted) c.fallback("forts.plot.capacity", "The contained plot limits surplus crops.", p);
        if (placed < 32) return "A fort has insufficient fertile farm space.";
    }
    for (let team = 0; team < c.request.teams; team++) for (let fruit = 0; fruit < 3; fruit++) {
        const home = homes.get(team), u = plan.orchardU + fruit * plan.orchardDu,
            v = plan.orchardV + fruit * plan.orchardDv;
        const seed = t.at(Math.trunc(home.x) + d.x(u, v), Math.trunc(home.y) + d.y(u, v));
        const eligible = i => homeOf.get(i) === team && plot.get(i) < 0 && !roads.get(i) && !reserved.get(i) && clear(i);
        const wanted = scaled(3, o["fruit-amount"]);
        const placed = eligible(seed) ? k.Planting.growPatch(t, seed, CHERRY + fruit, wanted, eligible) : 0;
        c.measure("forts.orchard.target-tiles", wanted, 3 * team + fruit);
        c.measure("forts.orchard.planted-tiles", placed, 3 * team + fruit);
        if (placed < wanted) return "A fort has insufficient household orchard space.";
    }
    const uplands = L.uplands;
    const forestAt = i => uplands.get(i) / 65536;
    const fields = k.LatticeNoise.PeriodicNoise(t.w, t.h, 16, c.stream("forts-fields"));
    const country = i => !buffer.get(i) && !towns.get(i) && !roads.get(i) && clear(i);
    let woods = [];
    for (let i = 0; i < t.size(); i++) if (country(i)) woods[woods.length] = i;
    woods.sort((a, b) => forestAt(b) - forestAt(a));
    const rocks = Math.min(woods.length, scaled(woods.length / 100, o["stone-amount"]));
    for (let j = 0; j < rocks; j++) c.setResource(woods[j], STONE, 1);
    woods = woods.slice(rocks);
    c.measure("forts.uplands.stone-tiles", rocks);
    const count = Math.min(woods.length, scaled(woods.length / 6, o["wood-amount"]));
    for (let j = 0; j < count; j++) c.setResource(woods[j], WOOD, 1);
    c.measure("forts.forest.planted-tiles", count);
    k.Homes.furnishGround(t, fertility, country, i => fields.at(...xy(i)), forestAt,
        area => ({wheat: scaled(area / 18, o["wheat-amount"]), wood: 0,
            stone: scaled(area / 500, o["stone-amount"]), fruit: scaled(area / 600, o["fruit-amount"])}),
        "forts-rocks", "forts-orchards");
    for (let j = 0; j < villages.length; j++) for (let fruit = 0; fruit < 3; fruit++) {
        const v = villages.get(j), angle = fruit * 2 * Math.PI / 3;
        const seed = k.Planting.seedNear(t, Math.trunc(v.x + (o["village-size"] + 4) * Math.cos(angle)),
            Math.trunc(v.y + (o["village-size"] + 4) * Math.sin(angle)), 8, country);
        if (seed >= 0) k.Planting.growPatch(t, seed, CHERRY + fruit, scaled(5, o["fruit-amount"]), country);
    }
    k.Planting.seedAlgae(t, "forts-algae", o["algae-amount"], k.Planting.AlgaeBand_anyWater());
    k.Pipeline.secureStartingCrops(t, 24, 32, 0, wall);
    k.Pipeline.reopenCrampedStarts({wheat: o["wheat-amount"], wood: o["wood-amount"], stone: o["stone-amount"],
        algae: o["algae-amount"], fruit: o["fruit-amount"]}, 24, 32, 0, wall);
}
export function validateWorld(c) { return c.toolkit.Blueprints.validateForts(); }
