# Round-trip save continuation

`round-trip-143.game.gz` is a binary save written at tick 1,200 by a format-143
master that routed by round trip (fetch plus carry), `06a106d3a`. It is a 64×64 headless world with two teams, each with an
inn, a swarm, scattered wheat and ten workers (`HeadlessGame`, seed 146). When it
was saved, 12 of the 20 workers were fetching and four buildings' round-trip
fields were live, so the save carries those fields in each building's
`roundTrip` section.

`LegacyRoundTripSave/*` loads it with the current engine, which reads and
discards the round-trip fields, and plays ticks 1,201–2,200 with greedy
fetching, salted private entity RNG streams, and scheduled building walking fields
(the save restores no pending
fields, and its pre-148 header gets the default eight-tick delay).
`round-trip-143-checksums.txt` is that per-tick trace (FNV-1a over every
checksum part except the MapHeader, whose version changes on save),
regenerated whenever the simulation changes. Re-saving at tick 1,700
in binary and text form, in the current format, must continue identically.
The old save has no PCG state: loading initializes every unit/building stream once
from its saved seed, GID and generation, and the new saves preserve that progress. Format 152 similarly initializes map
operation and SGSL story streams directly from the saved seed; the historical
world MT19937 record is preserved without executing a legacy RNG path.

The fixture was produced by a writer case built only in a master `06a106d3a`
tree; it is not part of the test suite because current code has no round-trip
fields to populate. Regenerating it is not expected: it represents saves that
already exist.
