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
    const type = types.find((t) => t.id === building.type);
    if (!type) continue;
    const hasTraining = type.training.some(service => service.enabled);
    const hasOtherWork = type.site || type.capabilities.some(capability => !capability.startsWith("train"));
    const disabledTrainingOnly = ctx.game.rules().noUpgrades && hasTraining && !hasOtherWork;
    building.workers = disabledTrainingOnly ? 0 : Math.min(2, type.maxWorkers);
  }
  ctx.telemetry.set(
    "decisions",
    decisions,
    "Decisions made by this controller",
  );
}
