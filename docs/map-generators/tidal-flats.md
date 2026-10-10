# Tidal flats

Grass islands standing on a wide expanse of walkable sand, with tide pools and lagoons over the
flats and the odd grassy sandbar. Sand carries units freely but holds no building and no deposit,
and nothing regrows beside it, so there are open-field battles from the first minute and no forward
bases: an army on the flats fights far from any inn or tower while a defender holds a rim of towers
on its island's edge, and expansion means taking another island whole.

- **Layout.** Every home island sits on a ring at 58% of the half side, one per colony evenly
  spaced from a random start, with a radius of `home-island-size` percent of the half side or as
  much as the ring, the wrap and the central island leave room for. Everything else -
  `extra-islands` neutral islands, `sandbars` and `lagoons` per colony and `tide-pools` per
  128×128 of flats - is placed in one wedge's frame, keeping clear of the home island, the wrap, the
  central island and each other, and stamped into every wedge alike, so the layout is fair for any
  colony count. `coast-roughness` shapes every island. `validateRequest` refuses a map whose home
  islands would shrink below nine tiles of radius.
- **Islands.** Each home has a pond at its middle, an unscaled kit of 40 wheat and 30 wood on the
  pond's two sides and a quarry towards the map's centre, then scaled ambient farmland on its
  fertile ground and an outcrop. Every neutral island is an oasis: a pond, and every other tile
  of it under unscaled wheat, so taking one means clearing it first, with one prize inside, a
  fruit grove or a stone deposit in turn. `extra-islands` defaults to six per wedge
  and `sandbars` to eight; an oasis is 30 to 45 percent of a home island's radius, never under 3.5 tiles, rounder
  than a home island, and keeps three tiles of sand from other features and four from a home island; sandbars, the green patches a forward post stands on, are 3.5
  to 5 tiles and keep the same gaps; and the scatter tries 240 times per feature. A 128 map
  with six colonies is the limit: its home islands nearly touch, and only an oasis or two of the
  smallest size fits between them and the central island. With `central-island` (on) an island at
  the centre carries a pond, the orchard of all three fruits and a quarry. Algae seeds every pool
  and lagoon, which is exactly where it regrows. Nothing is kept clear because the flats hold
  nothing.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every home's pond
  present and every colony and the central island reachable on foot from colony 0, with water,
  buildings and every resource blocking.
- **Rectangular maps.** The layout is designed in a circle on the map's shorter side and placed on
  the map through a stretched `WedgeFrame`, so on a rectangular map it fills the map as an ellipse,
  its islands, pools and lagoons stretched with it. Square maps are unchanged.

## Implementation source

[TidalFlatsGenerator.cpp](../../src/map/generator/generators/TidalFlatsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
