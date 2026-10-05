// A small, editable starting point. Build a strategy with your AI Studio assistant.
export function metadata() {
  return {
    apiVersion: 2,
    name: "My Colony",
    description: "A colony learning to grow",
    version: "1.0",
    author: "",
  };
}
let decisions = 0;
/** @param {import('./glob2-v2').ContextV2} ctx */
export function step(ctx) {
  decisions++;
  const types = ctx.game.buildingTypes();
  for (const building of ctx.game.buildings({ team: ctx.myTeam, limit: 50 })) {
    if (building.virtual) continue;
    const type = types.find((t) => t.id === building.type);
    const trainingOnly =
      type && ["school", "racetrack", "swimmingpool"].includes(type.name);
    building.workers =
      ctx.game.rules().noUpgrades && trainingOnly && !type.site ? 0 : 2;
  }
  ctx.telemetry.set(
    "decisions",
    decisions,
    "Decisions made by this controller",
  );
}
