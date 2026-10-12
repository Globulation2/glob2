# Artificial intelligence

Globulation 2 includes native opponents and portable JavaScript controllers.
Choose an AI in a player slot. The measured [ratings](ratings.md) explain strength
estimates and their cohort limits; they are not universal guarantees of difficulty.

## Opponents and implementation families

| Controller | Implementation |
| --- | --- |
| Numbi, Castor, Warrush | Older native controllers retained for compatibility and comparison. |
| Econo, Nicowar | Strategies hosted by the shared runtime. |
| Cortex | Native controller with optional trained policy modes. |
| Maxima | Modular colony economy, placement, farming and combat strategy. |
| Cabino | Independent specialists coordinating construction, upgrades and combat. |
| JavaScript | Imported script controller on the game's scripting interfaces. |

The authoritative registration and save dispatch are in
[AI](../../src/ai/AI.cpp) and its stable [implementation IDs](../../src/ai/AI.h).
Do not infer identities from menu ordering.

## Develop and understand an AI

1. [Change or add an AI](development.md): observation, orders, randomness and compatibility.
2. [Engine mechanics](engine-mechanics.md): services, training and upgrade downtime.
3. [Maxima](maxima/README.md): strategy, configuration and implementation references.
4. [Cortex training tools](../../tools/cortex-ml/README.md): optional policy experiments.
5. [JavaScript scripting](../scripting/javascript.md): script authoring and interfaces.

## Measure behavior

- [AI telemetry](telemetry.md): decision counters, fields and retained observations.
- [Gameplay measurements](gameplay-statistics.md): population, food, production and presentation.
- [Ratings](ratings.md): measured opponent strength and interpretation.
- [Win probability](architecture/win-probability-model.md): fitted game-state predictions.
- [Building-field depth](architecture/building-gradient-depth-model.md): scheduled field-settling model.
- [Tournaments](../tools/tournaments.md): reproducible distributed games and evidence.

Game rules and content contracts can change what an AI can do. Start with the
[map economy rules](../map-generators/game-rules-for-map-design.md) and
[building semantics](../features/building-semantics.md) when diagnosing a stall.
