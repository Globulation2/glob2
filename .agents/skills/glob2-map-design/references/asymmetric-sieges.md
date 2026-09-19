# Deliberately asymmetric maps and siege playtests

Read this when a map gives one colony a special role, when a perimeter has several
promised approaches, or when a rich-looking start stalls in AI games. These lessons
come from [Encircled Kingdom's implementation and evidence](../../../../docs/artifacts/encircled-kingdom/README.md).
Its numerical budgets are examples, not new requirements for other generators.

## Preserve roles while varying the map

Write down who is special, what advantage the terrain supplies, and how opponents
can contest it. Encircled Kingdom keeps colony zero in a large agricultural
heartland with ordinary starting units; outside towns share land fronts and can
invest in swimming. The engine's supported colony limit still applies: document
the actual envelope rather than promising unbounded player counts.

Shuffle only interchangeable starts. Compare the capital's food potential and
building opportunity with each individual outer town, then separately measure
coalition access and pressure. An aggregate equality score cannot establish the
intended role advantage. Keep that advantage in the generator's own validation;
a lobby's candidate selection must not accidentally choose it away.

For role-sensitive playtests, keep alliances and roles fixed while varying the AI
assigned to each role, the seed, and the number of opponents. Rotate equivalent
outside seats to investigate positional bias. Rotating everyone through the
capital answers a different question from whether the human's intended colony-zero
experience works. Group results by role and AI instead of pooling win rates.

When attackers outnumber fronts, measure their actual approach walks and the load
per front. A balanced assignment with a bounded walking distance is one useful
construction; equal angular spacing alone can leave a town far from every usable
gate. Such an assignment is a map measurement, not an order the AI will follow.

## Prove each promised approach

Global connectivity can pass while one advertised gate is unusable and another
road has accidentally filled the moat. Retain separate masks for each gate and
waterfront, as well as their union. On the finished world:

- Close every land gate and check the intended land separation.
- Reopen one gate at a time and verify its promised connection with the other
  crossings closed.
- Test each swimming approach separately, including both landing areas and access
  through any stone perimeter; specify which other approaches are closed.

Protect water as well as walls when drawing later roads. A route repair that paints
sand across a moat outside a designated gate changes the tactical contract.
Measure routes again after resources and settlements are placed. If a search for a
nearby walkable source returns no tile, handle that failure before indexing a grid;
concave outlines and rectangular maps expose assumptions hidden by square previews.

## Resource access has several meanings

A resource can be reachable by a worker yet fall outside an AI's local observation
area. Inspect the relevant [AI code](../../../../src/AIMaxima.cpp) and
[strategy](../../../../data/maxima/base.strategy) for the distance metric, radius,
and coordinate origin: a scan from a building's top-left is different from one
from its centre, and neither is a walking-distance test.

Encircled Kingdom's weak Maxima town had reachable wood but none in the local
20-tile observation radius. Moving the swarm slightly and planting sixteen tiles
of the existing timber budget nearby repaired that concrete weakness. The same
save also contained unfinished inns beside a distant ally. Nearby timber did not
fix that separate AI placement behavior, and more total wood alone did not establish
a remedy. Treat those numbers as a verified case, and recheck current AI code
before using them elsewhere.

Check worker access and AI visibility separately on actual settled coordinates,
including scarce-resource settings and every home variant. Allow for the swarm,
workers and harvesting frontage when measuring walk distance. Trace a stalled
colony from resource observation through harvesting, delivery and construction
before changing abundance or granting extra units.

## Measure opportunity separately from current obstruction

Resource tiles block walking, but harvesting can open crop ground. Use actual
initial passability for attack routes and starter access. For long-term food
potential, state whether the catchment allows the intended harvestable plots;
otherwise planting more wheat can make the reported food opportunity *decrease*
by blocking the measurement's flood. Keep both metrics when they answer different
questions, and check unchanged farmland at several planting amounts.

A sand-contained expansion plot with zero seeds may never become productive:
containment also prevents immigration from another plot. If expansion agriculture
is promised at zero abundance, give each plot an explicit seed floor or another
verified colonization mechanism. Document that floor separately from surplus.

## Demonstrate a siege and preserve the evidence's scope

Record first damage, destroyed buildings, births, resource delivery and starvation
by role. Starvation after inns are destroyed supports a different explanation from
starvation before first contact. A populous capital and peaceful attackers prove
establishment, not a working siege; look for actual invasion or counterattack and
inspect the late terrain for readable fields, roads and approaches.

Retest the weak request after a remedy, including its actual generated seed and
pinned design. A lobby selection seed can choose a different terrain candidate in
a new build. Preserve the starting map or the selected candidate's full request
for comparisons. Report changes in attack trajectory and slow economies even when
the targeted opening improves.

Label every game by build or source snapshot and terrain revision. Earlier long
games remain useful developmental evidence, but are not long games on the final
terrain. A final opening test is not a completed siege. If an AI crashes, reproduce
it on a reference map before attributing it to the generator; record reduced AI
coverage rather than changing unrelated simulation code to finish the map study.
