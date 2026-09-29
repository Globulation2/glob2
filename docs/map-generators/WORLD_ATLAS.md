# The world atlas

Real geography, compiled into the client: one small categorical raster per continent that the
Continents landscape (and any generator that wants real coasts, lakes, deserts, ranges and
biomes) fits onto a map. `src/map/generator/shared/WorldAtlasData.cpp` is generated; nothing
reads a file at run time, so a map stays a function of the request and the seed on every
platform.

## Sources and licences

| Source | What it supplies | Licence |
| --- | --- | --- |
| [Natural Earth](https://www.naturalearthdata.com/) 1:50m vector data, fetched as GeoJSON from the [nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector) mirror: `ne_50m_land`, `ne_50m_lakes`, `ne_50m_rivers_lake_centerlines`, `ne_50m_glaciated_areas`, `ne_50m_geography_regions_polys` | Coasts, lakes, the great rivers, ice caps, and the named deserts and mountain ranges | Public domain |
| Köppen-Geiger climate classification, Kottek, Grieser, Beck, Rudolf and Rubel (2006), "World Map of the Köppen-Geiger climate classification updated", *Meteorologische Zeitschrift* 15, 259-263; the 0.5° ASCII grid from [koeppen-geiger.vu-wien.ac.at](http://koeppen-geiger.vu-wien.ac.at/) | Biomes: which land is forest, farmland, steppe, desert, tundra or ice | Free with attribution (credited in `data/authors.txt`) |

Why these: the 1:50m scale resolves a continent at 512 pixels across (about 15 km a pixel)
without hair-thin coastlines, and a 0.5° climate grid (about 55 km) is coarse enough to read as
regions of a game map rather than speckle. Natural Earth's named deserts and ranges override the
climate classes, so the Sahara, Gobi, Rockies, Andes, Alps and Himalayas are where a player
expects them.

## Regenerating the data

```sh
python3 tools/world_atlas.py                                     # rewrites WorldAtlasData.cpp
python3 tools/world_atlas.py --preview artifacts/world-atlas/previews   # one PNG per continent (needs PIL)
python3 tools/world_atlas.py --only europe --preview artifacts/world-atlas/previews
```

The sources are downloaded once into `artifacts/world-atlas/cache` (ignored by Git; `--cache`
moves it). The script is plain Python 3: polygons are filled by an even-odd scanline, so holes need
no special handling, and nothing but PIL for previews is required. The output is deterministic for
a given cache. **The data is part of every map built on it**: when it changes, bump the revision of
every generator that uses it (Continents), re-record their golden rows, and say so in the PR.

## What a region holds

Each region (`north-america`, `south-america`, `africa`, `europe`, `asia`, `oceania`) is a raster
512 pixels across its land, cropped to the land plus a one-pixel border of sea, run-length encoded
as (value, run) byte pairs. A value is a `LandClass` (`WorldAtlas.h`) in its low nibble and a river
flag in bit 4:

| Class | From | Continents draws it as |
| --- | --- | --- |
| Ocean, Lake | Natural Earth land and lakes | Water |
| Plain | Köppen C*, Dfa/Dfb, Dwa/Dwb, Dsa/Dsb: the climates people farm | Grass: wheat country (the `farmland` kit) |
| Forest | Köppen Af, Am (rainforest) and Dfc-Dwd, Dsc (taiga) | Grass under wood cover (`woodland`) |
| Steppe | Köppen Aw, As, BSh, BSk: savanna and steppe | Grass with sparse crops (`savanna`) |
| Desert | Köppen BWh, BWk, and Natural Earth's named deserts | Sand |
| Mountain | Natural Earth's named ranges | Grass under stone scree (`highland`) |
| Tundra | Köppen ET | Grass that grows little (`barrens`) |
| Ice | Köppen EF and Natural Earth's glaciated areas | Sand |
| River flag | Natural Earth rivers of rank 1 to 3 (about a hundred worldwide) | A line of pure water a tile wide, with fords |

The continents are cut from each other along conventional lines drawn in `tools/world_atlas.py`
(the Aegean, Bosphorus, Caucasus and Urals between Europe and Asia; Suez and the Red Sea between
Africa and Asia; Panama's eastern border between the Americas; New Guinea's Bird's Head between Asia
and Oceania). Greenland, Hawaii, Antarctica and the Aleutians beyond 168° W are left out. North
America, Europe and Asia are drawn in an Albers equal-area conic projection, the near-equatorial
continents in an equirectangular one scaled by the cosine of their middle latitude, so each looks
like its atlas page. Adjust a box, a divide or the projection rule in the script's `CONTINENTS`
table; adding a region is adding a table entry and an id to the generator's choice list.

## Fitting a region onto a map

`shared/Raster.h` fits a source raster inside a map less a sea margin, keeping its aspect ratio,
centred, optionally turned a quarter turn when that fits a rectangular map larger, and resamples
it tile by tile: `resampleMajority` takes each tile's most common class (sea wins a tie, so
shrinking never fattens a coast) and `resampleAny` keeps a one-cell river through any shrink.
`shared/Landmass.h` then makes the coast playable: land narrower than three tiles goes (a strip two
wide is beach on both sides and grass nowhere), pools too small to be anything but beach are
filled with the land round them, islets too small to build on vanish, and `largestRegion` finds
the mainland every colony must share. All of it is integer arithmetic, so the same picture lands on
the same tiles on every platform; the golden rows for Continents are identical on Linux and macOS.
