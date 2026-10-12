# Generator catalog

The registry defines available generators and their current product order. Stable
text IDs select a generator in commands; legacy numeric IDs preserve authored-map
compatibility. Neither ID determines display order, and retired IDs are never reused.
Uniform is editor-only. Requests still need to satisfy each landscape's size,
colony-count and control constraints.

Use `glob2 info catalog --format json` for current controls, revisions and execution
capabilities, or `glob2 map generators` for the map CLI catalog. This table
indexes every constructor registered in
[GeneratorRegistry](../../src/map/generator/core/GeneratorRegistry.cpp); links lead
to a maintained design guide or directly to its implementation when no separate
guide is warranted.

| Landscape | Command ID | Legacy ID |
| --- | --- | ---: |
| [Allotments](allotments.md) | `allotments` | 40 |
| [Amphitheatre](amphitheatre.md) | `amphitheatre` | 23 |
| [Anthill](anthill.md) | `anthill` | 32 |
| [Bajada](bajada.md) | `bajada` | 55 |
| [Bastion Keys](bastion-keys.md) | `bastion-keys` | 68 |
| [Braided Delta](braided-delta.md) | `braided-delta` | 36 |
| [Braided river](braided-river.md) | `braided-river` | 42 |
| [Breachable highlands](breachable-highlands.md) | `breachable-highlands` | 37 |
| [Canals](canals.md) | `canals` | 29 |
| [Caravanserai](caravanserai.md) | `caravanserai` | 41 |
| [Carousel](carousel.md) | `carousel` | 22 |
| [Central Quarry](central-quarry.md) | `central-quarry` | 56 |
| [City states](city-states.md) | `city-states` | 17 |
| [Concrete islands](../../src/map/generator/generators/ConcreteIslandsGenerator.cpp) | `concrete-islands` | 5 |
| [Continents](continents.md) | `continents` | 44 |
| [Contested commons](../../src/map/generator/generators/ContestedCommonsGenerator.cpp) | `contested-commons` | 9 |
| [Coral](coral.md) | `coral` | 21 |
| [Crater lakes](../../src/map/generator/generators/CraterLakesGenerator.cpp) | `crater-lakes` | 4 |
| [Drowned Forest](drowned-forest.md) | `drowned-forest` | 67 |
| [Drumlin field](drumlin-field.md) | `drumlin-field` | 43 |
| [Emoji](emoji/design.md) | `emoji` | 34 |
| [Encircled Kingdom](encircled-kingdom.md) | `encircled-kingdom` | 63 |
| [Even Ground](even-ground.md) | `even-ground` | 60 |
| [Everglades](everglades.md) | `everglades` | 19 |
| [Fingerprint](fingerprint.md) | `fingerprint` | 26 |
| [Fjord continent](fjord-continent.md) | `fjord-continent` | 12 |
| [Forts](forts.md) | `forts` | 35 |
| [Hedgerow Country](hedgerow-country.md) | `hedgerow-country` | 38 |
| [Hidden Oasis](hidden-oasis.md) | `hidden-oasis` | 57 |
| [Hilbert River](fractal-maps.md) | `hilbert-river` | 50 |
| [Hills](hills.md) | `hills` | 46 |
| [Honeycomb isle](honeycomb-isle.md) | `honeycomb-isle` | 53 |
| [Islands](../../src/map/generator/generators/IslandsGenerator.cpp) | `islands` | 3 |
| [Isles](../../src/map/generator/generators/IslesGenerator.cpp) | `isles` | 6 |
| [Karst towers](karst-towers.md) | `karst-towers` | 54 |
| [Lava shield](lava-shield.md) | `lava-shield` | 51 |
| [Locust](locust.md) | `locust` | 47 |
| [Marchland](marchland.md) | `marchland` | 61 |
| [Maze](maze.md) | `maze` | 11 |
| [Old growth](old-growth.md) | `old-growth` | 28 |
| [Old islands](../../src/map/generator/generators/RuggedArchipelagoGenerator.cpp) | `rugged-archipelago` | 8 |
| [Old random](../../src/map/generator/generators/ShatteredCoastGenerator.cpp) | `shattered-coast` | 7 |
| [Old town](old-town.md) | `old-town` | 31 |
| [Orchard Commons](orchard-commons.md) | `orchard-commons` | 58 |
| [Plantations](plantations.md) | `plantations` | 48 |
| [Polder](polder.md) | `polder` | 30 |
| [Portage Lakes](portage-lakes.md) | `portage-lakes` | 65 |
| [Rain shadow](rain-shadow.md) | `rain-shadow` | 27 |
| [Rice terraces](rice-terraces.md) | `rice-terraces` | 52 |
| [Ring world](ring-world.md) | `ring-world` | 16 |
| [River](../../src/map/generator/generators/RiverGenerator.cpp) | `river` | 2 |
| [Savannah](savannah.md) | `savannah` | 45 |
| [Sierpiński Gardens](fractal-maps.md) | `sierpinski-gardens` | 49 |
| [Spider web](spider-web.md) | `spider-web` | 20 |
| [Stone highlands](stone-highlands.md) | `stone-highlands` | 14 |
| [Swamp](../../src/map/generator/generators/SwampGenerator.cpp) | `swamp` | 1 |
| [Switchbacks](switchbacks.md) | `switchbacks` | 24 |
| [Symmetric arena](symmetric-arena.md) | `symmetric-arena` | 15 |
| [The Comb](comb.md) | `comb` | 62 |
| [The Faulted City](faulted-city.md) | `faulted-city` | 64 |
| [The Gauntlet](gauntlet.md) | `gauntlet` | 59 |
| [The Glacis](the-glacis.md) | `glacis` | 39 |
| [The Hungry Marches](hungry-marches.md) | `hungry-marches` | 69 |
| [The Last Treeline](last-treeline.md) | `last-treeline` | 70 |
| [Tidal flats](tidal-flats.md) | `tidal-flats` | 18 |
| [uniform terrain](../../src/map/generator/generators/UniformGenerator.cpp) | `uniform` | 0 |
| [Watershed](watershed.md) | `watershed` | 13 |
| [Who Ate the Map?](who-ate-the-map.md) | `who-ate-the-map` | 66 |

See [map tools](cli.md), [framework](map-generator-framework.md) and [verification](verification.md).

Related: [map generators](README.md).
