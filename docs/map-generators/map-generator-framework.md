# Map generator framework

A generator is a pure function, `bool generate(Game &, GenerationContext &)`, registered
alongside its metadata in a `GeneratorDefinition` (id, stable legacy numeric id, display name,
revision, its `GeneratorControl`s, and optional `validateRequest`/`validateWorld` callbacks).
`GeneratorRegistry::builtins()` holds the fixed list; `GenerationService` drives the actual
lifecycle (validate the request, sample candidate seeds, run `generate`, score the result,
validate the finished world). There is no generator superclass or pipeline dispatcher: a
generator is a function that calls the shared stages below in whatever order its map needs, and
a designed generator's body is a short sequence of them. What a generator keeps to itself is its
design — the layout as a pure function of the request — and the few routines that only make sense
for its map.

## The designed generator

Designed generators commonly separate layout construction from rasterization and validation.
Their stages can be composed in the order the landscape needs:

1. `design(request, context)` computes the whole layout from the request and the context's
   named streams without touching the map, and returns it with a `failure` string when the
   request leaves no room.
2. `generate` stamps the layout into a `TerrainSketch`, calls `layBeaches` and `writeVertices`,
   then `settleColonies` with a home mask and an anchor per colony. The design has already dealt its
   start sites to the colonies at random (`dealStarts`), so which team gets which home is a draw:
   without it, farthest-point spreading and lattices hand team 0 the same ground on every map.
3. The kits go down with `plantKit` (three seeds, each grown from the nearest eligible
   tile), then the ambient layers
   (`scatterResources`, or the generator's own ranking fed to `plantFields` and `scatterClumps`,
   `seedAlgae`, `stockIslands`), then `secureStartingCrops`: `clearAroundSwarms`,
   `guaranteeStartingResources` and `clearAroundSwarms` again.
4. `openRoad` (or the generator's own cheapest-walk variant) keeps every walk the map promises
   open, clearing only the deposits in the way; `reopenCrampedStarts` runs at non-default amounts
   (`openStartsBuriedByResources` for a landscape that wants that relief at the defaults too).
5. `validateWorld` calls `design` again on a fresh context, checks it with `designMismatch`,
   walks every colony from colony 0 with `walkFromFirstColony`, and then checks whatever the
   design promised: ponds present, walls standing, fords open, symmetry exact.

A generator with a different order calls the same stages in its own order; a generator with a
different need (Ring world's belt-wide road, Symmetric arena's orbit stamping) writes that one
piece itself and says why in its header comment.

## Parameter search domains

`GeneratorControl::values()` is the legal manual domain; `searchValues()` is the declared
subset used by `GenerationRequest::randomizeControls()`. The generator registry requires a
valid search declaration on every control. Search metadata is emitted as `searchValues` in
both catalog APIs; the text catalog lists it after the legal values. Shared settings have
metadata for study tools but player-facing parameter rolls preserve them.

Maze, Canals, Honeycomb isle, Fingerprint (pattern and barrier), and Caravanserai (desert design)
default to an appended Random choice. Existing concrete enum numbers are unchanged. New
choices resolve from the map seed independently of geometry streams; geometric choices filter
out incompatible tessellations before selection. Parameter rolls select concrete variants.

Control studies accept `--domain search` (the default) and `--domain legal`. The latter retains
experimental extremes for compatibility studies. The profiling fixture has the equivalent
`--domain=search` and `--domain=legal` switches. A search envelope targets the generator's
supported shared setups, rather than promising every colony count fits every map size.

Search domains avoid unsuitable extremes; request and world validators still enforce each
generator's supported geometry. Individual AI openings are diagnostic evidence, not grounds
to discard a design variant without a reproducible parameter-related defect. Resource growth,
AI decisions and seed variation still affect games within the search envelope.

## Related references

- [Shared toolkit](toolkit.md): geometry, topology, terrain, planting and site operations.
- [Resources and starts](resources-and-starts.md): supply budgets and colony placement.
- [Generator catalog](catalog.md): identities and landscape families.
- [Verification](verification.md): revisions, contracts and compatibility.
- [Recursive layouts](fractal-maps.md): crossing and subdivision interfaces.
- [Adding a generator](adding-a-generator.md): registration and module workflow.

Related: [map generators](README.md).
