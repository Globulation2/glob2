# Repeat a starting map

Repetition is an advanced setup tool. In the premade library, open **Size and
parameters**; for a generated preview, expand **Layout** and open **Size and
parameters** there. The editor's map chooser offers the same collapsed controls.
The dialog scrolls on phones and uses the normal touch-sized choices and steppers.
Repeat controls follow the colony/base fields and start at **1** in both directions;
the generated map action follows the landscape-specific Layout fields.

Horizontal and vertical repeats are powers of two, bounded by the engine's
512-tile side limit. **Colonies** is the number of players; **bases** is the number
of source colonies assigned to each player. Copies are dealt evenly, with any
unused bases removed. The preview shows the resulting size, retained bases and
team colours. **Reset** restores the source; Cancel leaves the current setup alone.
Accepting a generated repeat freezes the previewed roll as a premade map.

Terrain, resources and team-painted areas (including experimental farm areas)
repeat with the source. Buildings, standing units and their areas wrap around
each base's anchor so bases crossing a seam stay together. This can change pacing and strategic routes: repeated
terrain does not certify balanced expansion or equally strong source colonies.

Only unscripted starting maps can be repeated. Saves and authored scripts are
refused because their runtime state and team/coordinate references cannot be
remapped safely. Units inside buildings, in-progress upgrades and team resource
counters are not copied. Placement failures refuse the result instead of silently
losing units or buildings. The editor offers 32-tile source maps; the game
lobby's generator retains its 64-tile minimum, and landscapes can refuse tiny
editor requests that cannot fit their colonies.

Generated source and result files live under the writable profile's `generated/`
directory, outside the authored map library, on native and mobile hosts. The
result loads, saves and transfers as an ordinary map, without a new save format.
These files remain available while lobbies and editors may still refer to them.

Related: [map generators](README.md).
