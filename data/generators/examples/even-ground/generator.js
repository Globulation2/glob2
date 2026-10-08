// SPDX-License-Identifier: GPL-3.0-or-later
// The native solver supplies the design; this script owns terrain, settlements and furnishing.
export function validateRequest(c) {
    return c.toolkit.Blueprints.evenGroundRequestFailure();
}
export function generate(c) {
    const k = c.toolkit, o = c.request.options;
    c.stage("even ground solve");
    const L = k.Blueprints.evenGroundDesign();
    if (L.failure) return L.failure;
    const t = L.t, lat = L.lat, home = L.home, homeOf = L.homeOf;
    const kind = L.kind, cellOf = L.cellOf, pinned = L.pinned;
    c.addTeams();
    c.stage("even ground terrain");
    k.Sketch.writeVertices(L.terrain);
    c.stage("even ground colonies");
    if (!k.Pipeline.settleColonies("even-ground-starts", team => {
        const mask = c.mask();
        for (let i = 0; i < mask.length; i++)
            mask.set(i, homeOf.get(i) === team && c.buildable(i) ? 1 : 0);
        return mask;
    }, team => {
        const cell = home.get(team);
        return k.LegacyRegions.MapGeneratorPoint((cell % lat.w) * lat.tiles + lat.tiles / 2,
            Math.trunc(cell / lat.w) * lat.tiles + lat.tiles / 2);
    })) return "Could not place colonies";
    c.stage("even ground resources");
    const reserved = k.Planting.swarmSurroundings(t);
    const grain = k.LatticeNoise.fractalNoise(t.w, t.h, Math.max(4, lat.tiles), 3, c.stream("even-ground-cover"));
    const ground = [[], [], [], [], []];
    for (let i = 0; i < t.size(); i++) {
        const cell = cellOf.get(i), type = kind.get(cell);
        if (type >= 2 && !pinned.get(cell) && !reserved.get(i) &&
            k.Planting.clearGround(i % t.w, Math.trunc(i / t.w))) ground[type][ground[type].length] = i;
    }
    const level = i => grain.get(i);
    c.measure("even-ground.wheat.tiles", k.Planting.plantCoverShare(t, ground[2], 1, 32, level));
    c.measure("even-ground.wood.tiles", k.Planting.plantCoverShare(t, ground[3], 0, 26, level));
    c.measure("even-ground.stone.tiles", k.Planting.plantCoverShare(t, ground[4], 3, 10, level));
    k.Planting.seedAlgae(t, "even-ground-algae", o["algae-amount"], k.Planting.AlgaeBand_shallows(1, 4).thriving(0.5));
    c.stage("even ground openings");
    k.Pipeline.secureStartingCrops(t);
    k.Roads.openColonyRoutes(t, {open: 1, clearable: 3, eternal: -1, water: 8, building: -1});
    k.Pipeline.reopenCrampedStarts({wheat: o["wheat-amount"], wood: o["wood-amount"],
        stone: o["stone-amount"], algae: o["algae-amount"], fruit: 100});
}
export function validateWorld(c) {
    const k = c.toolkit, teams = c.request.teams;
    const cut = k.Pipeline.walkFromFirstColony(teams, "the land", "").error;
    if (cut) return cut;
    return k.Pipeline.startingAccessFailure(teams, [
        {material: 1, range: 24, name: "food"}, {material: 0, range: 32, name: "wood"}
    ]);
}
