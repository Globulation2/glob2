# The Faulted City

The Faulted City (`faulted-city`, numeric ID 64) is a planned city broken into displaced neighbourhoods. Buildable grass avenues follow the
old street grid; broad sandy fractures form a permanent travel network. Junctions connect
the two. There is no central objective or special starting army: colonies settle suitable
lawns beside the city's existing irrigated gardens.

The source city is drawn once, then sampled through a separate integer translation in
each district of a coarse, warped toroidal tessellation. Source block identities persist
across fractures, so outlines and streets continue at different offsets on opposite sides.
The map fills the torus, including its seams. Empty courtyard shells and reclaimed ruins vary independently of district boundaries.
Markets, irrigated gardens, reservoir service courts and orchards have different layouts.
A three-block civic plaza crosses an interior fault: its two surviving pieces visibly
show the displacement, with arcade, fountain and market variants. At least six masonry
tiles of the plaza must survive in each of two districts.

## Controls

- **Fault displacement:** maximum translation on either axis, 4–14 tiles, default 10.
- **Fault width:** the sandy boundary radius is half this corner width, 8–16, default 12.
  Rasterization includes the boundary centreline; final walkable width is measured separately.
- **Ruin density:** percentage of source blocks assigned ruins, 20–60%, default 35%.
- **Surviving junctions:** two cleared avenue mouths per district plus zero, one or two extras
  (Few, Normal, Many). Normal is the default. Mouths are spread along existing streets;
  they remove local masonry detours. Natural gaps remain, so this is not a count of all
  possible district exits. Each mouth reserves a clear five-by-five tile area.
- **Resource amounts:** ordinary resource percentages scale eligible deposits. Structural
  stone remains even at zero. Every renewable tile of each selected starter wheat/wood plot is seeded;
  the sliders scale other plots and ambient resources. Other crop plots fill to 80%
  of renewable capacity at 100%, then increase to full capacity at 300%. Abundance never spills outside
  plots or onto streets, and decorative resources cannot obstruct junction reservations.

Dimensions are 256 or 512 tiles on each axis, with 1–16 colonies. Counts above twelve require 512×512. Unsupported dimensions
are rejected before generation. A particular seed can be refused if it lacks viable starts;
the normal lobby's candidate mechanism can choose another seed. The generator searches
up to eight deterministic city layouts before refusing a request. Layout and starting-site
selection use fixed maximum resource amounts, so resource sliders do not redraw terrain.

## Economy and fairness

Sand caps and dividers contain gardens physically; generated maps never disable growth.
Broader rear reservoirs irrigate the civic gardens; wider wood compartments
provide an ordinary construction economy rather than an unintended timber shortage.
Fertility is calculated on final terrain, after beaches. Stone outlines are permanent
resource deposits and supply ammunition as well as upgrades. They do not block tower fire.
Grass streets can be built upon; sandy fractures cannot. Swimming is optional.

Starts are found in existing lawns and ruined courtyards, with seven-by-seven tile
clearance, and checked as completed colonies. Several dispersed arrangements are compared
using the existing start-quality model. Each starter wheat plot has at least 48 fertile
tiles and three units of summed growth probability; no two colonies share that starter
wheat plot. Completed colonies need at least 12 accessible wheat deposits within 12 steps,
24 within 24 steps from their own starter plot, reachable starter wood within 24 steps,
and six separate reachable four-by-four building footprints outside future crop growth. Each
occupied interior component reaches at least two cleared junctions before entering the
fault network. This is measured fairness, not symmetry. Team
indices are randomly dealt after selecting the sites. AI games and human review remain
necessary to assess positions, logistics and the value of junctions.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[FaultedCityGenerator.cpp](../../src/map/generator/generators/FaultedCityGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
