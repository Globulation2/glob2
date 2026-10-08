// SPDX-License-Identifier: GPL-3.0-or-later
export function generate(c) {
    const terrain = c.toolkit.Terrain;
    const options = terrain.HeightFieldOptions_fromRequest(true);
    if (!terrain.generateHeightField(options, (hm, width, height, smoothing) => {
        c.choice("swamp.field.shape", "noise");
        c.measure("swamp.field.smoothing", smoothing);
        hm.makeSwamp(smoothing);
    })) return "Could not construct the height field or place starts";
    c.stage("starts");
    if (!c.toolkit.LegacyStartingPositions.placeStarts()) return "Could not place colonies";
    c.toolkit.Pipeline.openStartsBuriedByResources();
}
export function validateWorld(c) {
    return c.toolkit.Pipeline.startingFloorFailure(c.request.teams);
}
