# Verification scenarios

Start with [native tests](../../../test/README.md) for build commands, runner isolation, tags and adding cases. Use these focused runbooks for the behavior you change; [PR verification](../verification.md) defines evidence and coverage expectations.

## Engine and content

1. [Simulation and field scheduling](simulation.md).
2. [Save continuation and untrusted files](persistence.md).
3. [AI and tournament execution](ai.md).
4. [Maps, generators and runtime catalogs](maps.md).
5. [Rendering, UI and previews](rendering.md).
6. [Recording and audio](assets.md).
7. [JavaScript](scripting.md).
8. [Telemetry](telemetry.md).

## Integration and tooling

- [Online and LAN](network.md), [mobile platforms](platforms.md), and [browser tests](../../../browser/README.md#automated-tests).
- [Lobby automation](lobby-automation.md).
- [Python tooling](tooling.md) and [coverage workflows](ci.md).

Run commands from the repository root unless a guide says otherwise. Fixture references describe retained inputs and their format requirements; never regenerate fixtures merely to hide an unintended simulation change.
