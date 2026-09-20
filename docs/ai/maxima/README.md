# Maxima

Maxima develops a sustainable colony, trains an army and sends gathered waves
against enemy settlements. Select `maxima` in a player slot or the normal AI menu.

The strategy director observes the colony, terrain and fog-visible opponents,
then allocates development, food, defence, reconnaissance and attack budgets.
Policy modules execute those decisions through Maxima's private runtime using
the engine's ordinary orders. Building lifetime identities keep pending work
from attaching to a different building that reuses a slot.

- [Economy and staffing](economy.md): worker allocation, production and development.
- [Farming](farming.md): renewable resources, access and clearing.
- [Food capacity](food.md): supply claims, placement, retirement and relocation.
- [Combat and reconnaissance](combat.md): force estimates, attacks and fruit missions.
- [Configuration](configuration.md): parameter files, overrides and schema access.

All Maxima implementation lives in `src/ai/maxima/`, parameter files in
`data/maxima/`, and tests in [`test/maxima/`](../../../test/maxima/README.md).
Saved games retain the resolved strategy and execution state, including pending
orders, staffing, labour allowances, attack waves and observation history.

Loading Maxima state requires save format 115 or newer. Other AIs retain the
engine's ordinary save-format support.

The main `AIMaxima.cpp` coordinates observations, director budgets, development
and scheduling. `AIMaximaCombat.cpp` owns attack execution, defensive responses
and explorer strikes; `AIMaximaState.cpp` owns serialization. Farming, placement,
food accounting, reconnaissance, force inference and the runtime have separate
modules alongside them. `AIMaximaWorldHelpers.h` shares small visibility, power
and building-signature operations used by these modules.
