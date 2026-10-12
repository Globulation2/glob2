# Create content for Globulation 2

Choose the smallest content interface that expresses the change. Definitions and
artwork can compose existing engine capabilities; a new simulation primitive
needs an engine change and compatibility review.

## Choose a starting point

| Goal | Start here |
| --- | --- |
| Make a playable landscape | [Map generation and image import](../map-generators/cli.md); inspect the result with both a preview and `--render-game`. |
| Write a procedural generator | [JavaScript packages](../map-generators/javascript.md), or [native generator workflow](../map-generators/adding-a-generator.md). |
| Add a building family | [Building catalogs](../features/building-catalogs.md) and [portable family packages](../features/building-family-packages.md). |
| Add a deposit yielding existing materials | [Resource imports](../features/resource-catalogs.md) and [property reference](../features/resource-properties.md). |
| Change terrain appearance | [Terrain materials](terrain-materials.md); keep appearance separate from terrain gameplay properties. |
| Package a themed map palette | [Terrain/resource sets](../features/terrain-resource-sets.md). |
| Add scripted behavior | [JavaScript scripting](../scripting/javascript.md) and its API reference. |
| Produce artwork or music | [Artwork workflow](artwork-workflow.md) or [music pipeline](music-pipeline.md). |

## Build a small first example

Use a copy of the shipped definitions or a documented minimal package. Give
content stable keys; do not assign runtime IDs or overwrite stock definitions to
imitate an additive import. Import locally, inspect loader diagnostics and test
with the smallest map that demonstrates the feature. Check disabled/missing
capabilities and any required experiments.

For maps, inspect walking routes, starting food/wood and construction room, then
run a populated game. Static resource totals do not establish sustainable supply.
Read the [map design rules](../map-generators/game-rules-for-map-design.md) and
[service mechanics](../ai/engine-mechanics.md) before tuning around a stalled colony.

## Package and share

Use the content family's export path. Verify a package in a fresh profile so
unrelated installed definitions/artwork cannot hide missing dependencies. Check
save/reload and multiplayer transfer where supported. Include editable sources,
license/attribution and reproducible export recipes in their maintained locations;
keep candidate renders and run evidence under ignored `artifacts/`.

The optional online studios provide authoring workflows through the same native
validation contracts. A downloaded playable package should not require an AI
provider or editor service to run. Follow the specific package guide for limits
and bundled artwork; standalone definition files do not automatically transfer
installed sprites.

Related: [asset production](README.md).
