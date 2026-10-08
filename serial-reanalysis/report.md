# Merged simulation hot-path reanalysis

Revision: `6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf` (PR #963, all five commits). Linux x86-64, therig, GCC 13.4.0, normal release optimization and debug symbols; no -pg. Sampling includes user and kernel cycles. Loading, fixture generation, final saving and teardown are outside the measured window. All measured continuations match the archived final checksums and keep eight-tick scheduling delays.

## Owner work, other-thread CPU and joins

4,096 ticks per run. Sparse/dense controls have three native repeats; other fixtures have one reconnaissance run. Other builds were active on the shared host; host-load.json records each run. These are diagnostic timings under that load, not baseline/candidate speedup claims or an isolated executor-sizing benchmark. Other-thread CPU is process CPU minus owner CPU and may include small auxiliary-thread costs. Tiny negative residuals from successive clock reads are displayed as zero; raw values are retained. Joins are elapsed waits; they must not be added to CPU totals.

| Fixture | Participants | Repeats | Owner CPU s | Other CPU s | Wall s | Join elapsed s | Owner kernel sample share |
|---|---:|---:|---:|---:|---:|---:|---:|
| sparse | 4 | 3 | 0.631 | 2.121 | 1.298 | 0.676 | 9.5% |
| hiring | 4 | 1 | 0.946 | 2.774 | 1.539 | 0.601 | 4.0% |
| established | 4 | 1 | 2.556 | 12.490 | 7.556 | 5.022 | 4.6% |
| dense | 4 | 3 | 10.938 | 15.708 | 15.800 | 4.511 | 3.6% |
| combat | 4 | 1 | 4.580 | 16.332 | 14.783 | 7.572 | 10.0% |
| sparse | 1 | 3 | 4.588 | 0.000 | 5.982 | 0.000 | 2.7% |
| dense | 1 | 3 | 38.760 | 0.000 | 55.180 | 0.000 | 3.3% |
| sparse | 32 | 3 | 1.524 | 5.264 | 4.673 | 1.583 | 20.6% |
| dense | 32 | 3 | 19.941 | 22.915 | 31.449 | 7.682 | 7.4% |

## Process hardware counters

Counters cover all process threads during the continuation. Values are not normalized by host load. One participant executes worker jobs on the owner; its owner CPU therefore includes both roles.

| Case | Instructions (billions) | IPC | Context switches | Cache misses (millions) |
|---|---:|---:|---:|---:|
| sparse-4 | 14.64 | 1.48 | 11453 | 82.5 |
| hiring-4 | 16.19 | 1.38 | 9650 | 99.1 |
| established-4 | 85.11 | 1.41 | 14372 | 271.4 |
| dense-4 | 118.38 | 1.13 | 17617 | 555.9 |
| combat-4 | 73.88 | 0.87 | 32433 | 354.3 |
| sparse-1 | 14.25 | 0.88 | 2324 | 84.5 |
| dense-1 | 117.50 | 0.74 | 14844 | 687.5 |
| sparse-32 | 19.83 | 0.60 | 192210 | 182.6 |
| dense-32 | 124.29 | 0.69 | 274725 | 788.3 |

## Snapshot counters

End-of-run capture counters from the first native run, including the initial boundary capture (4,097 captures for 4,096 continued ticks). These are capture-accounted bytes, not all memory traffic or a timing measurement. Existing component reuse is active.

| Fixture | Copied GiB | Component reuses | Capture allocation count | Peak snapshot capacity MiB |
|---|---:|---:|---:|---:|
| sparse | 0.55 | 87185 | 545 | 7.1 |
| hiring | 0.76 | 105177 | 724 | 7.9 |
| established | 1.94 | 241154 | 937 | 12.5 |
| dense | 3.89 | 506597 | 1110 | 17.8 |
| combat | 1.74 | 236890 | 900 | 10.4 |

## Native sample leaders

Percentages below are normalized within the indicated role, not wall time. Self samples identify where CPU executes; inclusive caller paths overlap and must not be summed. Self reports resolve user and kernel symbols. Owner call graphs accompany them; kernel frames in those earlier unprivileged call graphs may remain raw addresses.

### sparse-4

| Role | Self sample share | Symbol |
|---|---:|---|
| owner | 9.9% | `__memmove_avx_unaligned_erms` |
| owner | 9.3% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| owner | 6.3% | `SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKindVie…` |
| owner | 4.8% | `Team::syncStep()` |
| owner | 4.7% | `Team::integrity()` |
| owner | 4.6% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| owner | 3.6% | `Map::propagateGradient(unsigned short*, int, int)` |
| owner | 3.3% | `Unit::locationIsInEnemyGuardTowerRange(int, int) const` |
| other_threads | 22.0% | `gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)` |
| other_threads | 5.1% | `gradient_preparation::MaterialSeedCache::trySeed(SimulationSnapshot::Handle const&, int, int, int, unsigned short*, unsigned short const*)` |
| other_threads | 4.9% | `AIMaximaFoodLedger::Ledger::walk(AIMaximaFoodLedger::Input const&, int, int, int, int, int, int, std::vector<AIMaximaFoodLedger::Ledger::ReachCell, std::allo…` |
| other_threads | 4.0% | `AIMaximaFoodLedger::Input::normalizeY(int) const` |
| other_threads | 3.0% | `AIMaximaFoodLedger::Input::normalizeX(int) const` |
| other_threads | 2.9% | `AI::decide(AIEngine::DecisionContext const&)` |
| other_threads | 2.8% | `AIMaximaPlacement::Planner::prepareScoringCaches(AIMaximaPlacement::WorldState const&) const` |
| other_threads | 2.5% | `ResourceGrowth::calculate(MapState::View const&, MersenneTwister&, ResourceGrowth::Batch&)` |

### hiring-4

| Role | Self sample share | Symbol |
|---|---:|---|
| owner | 10.9% | `SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKindVie…` |
| owner | 8.5% | `__memmove_avx_unaligned_erms` |
| owner | 6.5% | `Map::propagateGradient(unsigned short*, int, int)` |
| owner | 6.1% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| owner | 5.2% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| owner | 4.0% | `Team::syncStep()` |
| owner | 3.1% | `Team::integrity()` |
| owner | 3.0% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| other_threads | 29.1% | `gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)` |
| other_threads | 9.3% | `AIMaximaFruit::Field::build()` |
| other_threads | 6.9% | `gradient_preparation::MaterialSeedCache::trySeed(SimulationSnapshot::Handle const&, int, int, int, unsigned short*, unsigned short const*)` |
| other_threads | 3.7% | `_ZN5field12breadthFirstISt6vectorIiSaIiEEZN8AIMaxima21reachableFoodCapacityEPKN8AIEngine11AIWorldViewEPKN18SimulationSnapshot12BuildingViewEjbiRKNS4_7Farming…` |
| other_threads | 2.1% | `AIMaximaFoodLedger::Input::normalizeX(int) const` |
| other_threads | 1.9% | `AIMaxima::Maxima::worker_reachable_circulation(AIMaximaRuntime::Context&, bool) const` |
| other_threads | 1.8% | `AI::decide(AIEngine::DecisionContext const&)` |
| other_threads | 1.7% | `AIMaximaFoodLedger::Ledger::walk(AIMaximaFoodLedger::Input const&, int, int, int, int, int, int, std::vector<AIMaximaFoodLedger::Ledger::ReachCell, std::allo…` |

### established-4

| Role | Self sample share | Symbol |
|---|---:|---|
| owner | 12.0% | `SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKindVie…` |
| owner | 7.3% | `Map::propagateGradient(unsigned short*, int, int)` |
| owner | 6.5% | `__memmove_avx_unaligned_erms` |
| owner | 5.9% | `Unit::checkSum(std::vector<unsigned int, std::allocator<unsigned int> >*)` |
| owner | 2.2% | `Unit::locationIsInEnemyGuardTowerRange(int, int) const` |
| owner | 2.1% | `Building::integrity()` |
| owner | 2.1% | `Building::materialDeliveryNeed(int) const` |
| owner | 2.1% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| other_threads | 16.1% | `AIMaximaFoodLedger::Ledger::walk(AIMaximaFoodLedger::Input const&, int, int, int, int, int, int, std::vector<AIMaximaFoodLedger::Ledger::ReachCell, std::allo…` |
| other_threads | 10.6% | `AIMaximaFoodLedger::Input::normalizeX(int) const` |
| other_threads | 8.4% | `AIMaximaFoodLedger::Input::normalizeY(int) const` |
| other_threads | 7.6% | `gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)` |
| other_threads | 7.0% | `void BuildingGradientSearch::advance<BuildingGradientSearch::resolveToCost(int)::{lambda()#1}>(BuildingGradientSearch::resolveToCost(int)::{lambda()#1})` |
| other_threads | 3.8% | `AIMaximaPlacement::Planner::colonyFoodTiles(AIMaximaPlacement::WorldState const&, int, int, AIMaximaPlacement::Footprint const&) const` |
| other_threads | 3.7% | `AIMaxima::Maxima::worker_reachable_circulation(AIMaximaRuntime::Context&, bool) const` |
| other_threads | 3.6% | `_ZN5field12breadthFirstISt6vectorIiSaIiEEZN8AIMaxima21reachableFoodCapacityEPKN8AIEngine11AIWorldViewEPKN18SimulationSnapshot12BuildingViewEjbiRKNS4_7Farming…` |

### dense-4

| Role | Self sample share | Symbol |
|---|---:|---|
| owner | 9.0% | `SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKindVie…` |
| owner | 8.0% | `void BuildingGradientSearch::advance<BuildingGradientSearch::resolve(unsigned long)::{lambda()#1}>(BuildingGradientSearch::resolve(unsigned long)::{lambda()#…` |
| owner | 6.2% | `BuildingGradientSearch::resolve(unsigned long)` |
| owner | 4.0% | `Building::noteUnitFailing(Unit*, Building::UnitCantWorkReason)` |
| owner | 4.0% | `Unit::checkSum(std::vector<unsigned int, std::allocator<unsigned int> >*)` |
| owner | 3.5% | `Building::gatherBringMaterialsCandidates(Building::BringMaterialsCandidate*, int)` |
| owner | 3.5% | `Building::selectUnitCarryingWantedMaterial(int const*, int const*, Building::BringMaterialsSelection&)` |
| owner | 3.3% | `Building::considerUnitForBuilding(Unit*, int*)` |
| other_threads | 16.1% | `void BuildingGradientSearch::advance<BuildingGradientSearch::resolveToCost(int)::{lambda()#1}>(BuildingGradientSearch::resolveToCost(int)::{lambda()#1})` |
| other_threads | 11.7% | `AIMaximaFoodLedger::Ledger::walk(AIMaximaFoodLedger::Input const&, int, int, int, int, int, int, std::vector<AIMaximaFoodLedger::Ledger::ReachCell, std::allo…` |
| other_threads | 8.8% | `gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)` |
| other_threads | 7.8% | `_ZN5field12breadthFirstISt6vectorIiSaIiEEZN8AIMaxima21reachableFoodCapacityEPKN8AIEngine11AIWorldViewEPKN18SimulationSnapshot12BuildingViewEjbiRKNS4_7Farming…` |
| other_threads | 6.9% | `AIMaximaFoodLedger::Input::normalizeX(int) const` |
| other_threads | 6.3% | `AIMaximaFoodLedger::Input::normalizeY(int) const` |
| other_threads | 4.2% | `gradient_preparation::seedBuilding(gradient_preparation::BuildingSeed const&, SimulationSnapshot::Handle const&, unsigned short*)` |
| other_threads | 3.9% | `gradient_preparation::MaterialSeedCache::trySeed(SimulationSnapshot::Handle const&, int, int, int, unsigned short*, unsigned short const*)` |

### combat-4

| Role | Self sample share | Symbol |
|---|---:|---|
| owner | 11.1% | `SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKindVie…` |
| owner | 7.8% | `Map::propagateGradient(unsigned short*, int, int)` |
| owner | 4.6% | `__memmove_avx_unaligned_erms` |
| owner | 4.4% | `Unit::checkSum(std::vector<unsigned int, std::allocator<unsigned int> >*)` |
| owner | 3.3% | `Building::integrity()` |
| owner | 2.7% | `auto SimulationSnapshot::capture(Game const&, std::shared_ptr<std::vector<SimulationSnapshot::BuildingKindView, std::allocator<SimulationSnapshot::BuildingKi…` |
| owner | 2.3% | `Building::materialDeliveryNeed(int) const` |
| owner | 2.2% | `Building::checkSum(std::vector<unsigned int, std::allocator<unsigned int> >*)` |
| other_threads | 14.7% | `AIMaximaFoodLedger::Ledger::walk(AIMaximaFoodLedger::Input const&, int, int, int, int, int, int, std::vector<AIMaximaFoodLedger::Ledger::ReachCell, std::allo…` |
| other_threads | 7.5% | `gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)` |
| other_threads | 6.8% | `AIMaximaFoodLedger::Input::normalizeX(int) const` |
| other_threads | 5.9% | `AIMaximaFoodLedger::Input::normalizeY(int) const` |
| other_threads | 5.4% | `AIMaxima::Maxima::worker_reachable_circulation(AIMaximaRuntime::Context&, bool) const` |
| other_threads | 4.5% | `void BuildingGradientSearch::advance<BuildingGradientSearch::resolveToCost(int)::{lambda()#1}>(BuildingGradientSearch::resolveToCost(int)::{lambda()#1})` |
| other_threads | 3.3% | `AIMaximaFoodLedger::Input::index(int, int) const` |
| other_threads | 2.5% | `gradient_preparation::MaterialSeedCache::trySeed(SimulationSnapshot::Handle const&, int, int, int, unsigned short*, unsigned short const*)` |

## Scope

The previous GCC 15 Callgrind profiles of the five-patch combination are supplemental instruction/caller evidence against the original audit baseline. They predate later gradient-policy changes and are not substituted for the merged-revision native samples. The headless owner includes read-boundary capture, AI publication, order execution and simulation work; interactive rendering/input and network stalls need separate measurements. ARM64 was excluded by the updated local-only scope.

## Grouped self samples, four participants

Sums combine distinct self symbols in each named family and normalize within the owner or other-thread role. Snapshot capture excludes libc copy samples; copy samples include callers other than snapshot capture. These are sample shares, not predicted speedups. One profile per fixture; percentages are approximate.

| Fixture | Owner snapshot capture | Owner memcpy/memmove | Owner tower query | Owner building-gradient search | Worker X/Y normalization | Worker gradient propagation |
|---|---:|---:|---:|---:|---:|---:|
| sparse | 21.7% | 10.3% | 3.3% | 0.7% | 7.0% | 22.0% |
| hiring | 25.7% | 8.5% | 1.7% | 1.4% | 3.6% | 29.1% |
| established | 17.3% | 6.8% | 2.2% | 5.9% | 18.9% | 7.6% |
| dense | 9.8% | 3.0% | 0.5% | 17.1% | 13.2% | 8.8% |
| combat | 17.6% | 4.8% | 0.3% | 3.0% | 12.6% | 7.5% |
