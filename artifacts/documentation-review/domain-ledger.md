# Domain documentation dispositions

- Split map-generator-framework.md (1,954 lines) by reader task; preserved toolkit/resource/start/compatibility contracts and individual landscape implementations. Removed duplicated designed-generator paragraph and dated catalog-order history. Source: src/map/generator/core, shared and generators.

- Retired fractal-validation.md delivery diary; extracted incomplete-coverage limits and reproducible verification responsibilities into recursive-verification.md. Historical builds/results remain Git history, not live acceptance claims.

- Split building-catalogs.md into authoring and focused contracts/workflows; kept examples, compatibility and error behavior. Sources: data/buildings, data/resources, src/building/types/BuildingCatalog.cpp, src/resource/ResourceCatalog.cpp and platform package contracts.

- Split resource-catalogs.md into authoring and focused contracts/workflows; kept examples, compatibility and error behavior. Sources: data/buildings, data/resources, src/building/types/BuildingCatalog.cpp, src/resource/ResourceCatalog.cpp and platform package contracts.

- Corrected datasrc/gfx README and COVERAGE former-PR disclaimers. Preserved original archive hashes, source attribution, coverage uncertainties and export recipes; generated CATALOG and runtime ASSET-PROVENANCE stay untouched.

- Replaced cortex-upgrade-expand-mechanics.md stale investigation with engine-mechanics.md canonical source-backed reference: NB_ABILITY loop, capacity guard, service semantics, non-demoting target training, observations already added. Removed obsolete missing-plumbing recommendations and prompt-specific prose.

## Retired historical report sections

- docs/map-generators/marchland.md: Four things that were wrong on the way, Historical tournament
- docs/map-generators/hedgerow-country.md: Validation record
- docs/map-generators/braided-delta.md: Initial AI playtesting
- docs/map-generators/hills.md: Initial playtest tuning, Bulk generation repair
- docs/map-generators/breachable-highlands.md: Initial validation and tuning, Subsequent AI playtesting
- docs/map-generators/even-ground.md: What was measured, Four things that were tried and did not work, Historical tournament: position bias before the rename
- docs/map-generators/comb.md: Historical revision 2: scattered ground, Revision 1 baseline results, Baseline gameplay observations and limits

## Limitations

Commands have been checked against source where noted; documentation changes do not establish fresh runtime/platform test results. Generated provenance and original catalogs are intentionally unchanged.

## Second editorial pass

- Added ordered AI, feature, asset and generator hubs, source-backed AI workflow, content quick start and artwork workflow.
- Rebuilt generator index from current constructor metadata; all 68 registry entries are browsable through design guides or implementation sources.
- Split music packages from production guide; corrected deleted Cortex pilot references and scratch-directory policy.
- Removed historic Windows/torus acceptance narratives while preserving platform gaps and reproducible checks.
- Preserved map repetition guide from old index and retained immutable bundle ID cautions without stale delivery claims.

## Final report cleanup

- docs/map-generators/karst-towers.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/hedgerow-country.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/portage-lakes.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/braided-delta.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/who-ate-the-map.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/central-quarry.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/rice-terraces.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/orchard-commons.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/locust.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/hills.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/breachable-highlands.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/last-treeline.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/drowned-forest.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/savannah.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/gauntlet.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/hidden-oasis.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/forts.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/hungry-marches.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/faulted-city.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/honeycomb-isle.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/lava-shield.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/bajada.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.
- docs/map-generators/comb.md: replaced historical Verification with current verification responsibilities and retained durable gameplay caveats.

- Source review corrected Nicowar twelve-colony stale crash warning: current SearchTools.cpp bounds enemy iteration against observation team count, and Team::MAX_COUNT is 16.
- Runtime manifest has 3,441 frames. Preexisting runtime_provenance.py --check failed on procedural resource recipes. Added only category recognition, regenerated the Markdown inventory and passed its source/output-hash and current-output check; no assets modified.
- Current registered generator inventory has 68 entries. Source comments between fields originally hid Continents from the extraction; fixed explicitly.


## Per-page retained source and disposition ledger

This inventory covers every retained owned Markdown page, including generated/source inventories. Source review is static; figures from retained immutable calibration cohorts remain historical evidence. No new gameplay, release, cross-platform or network acceptance is claimed.

| Page | Source of truth / reviewed interface | Disposition and verification boundary |
| --- | --- | --- |
| `data/highres/v1/README.md` | `data/highres/v1/manifest.json`, `tools/package_assets.py`, `tools/artwork/package_runtime.py`, `tools/artwork/ai/README.md` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `datasrc/gfx/CATALOG.md` | `tools/artwork/catalog_originals.py`, `datasrc/gfx/tools/units/rename_globules.py`, `datasrc/gfx/originals/buildings/barracks/level-1/__building5.xcf`, `datasrc/gfx/originals/buildings/barracks/level-3/__building12.xcf` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/README.md` | `tools/artwork/import_originals.py`, `tools/artwork/import_more_originals.py`, `tools/artwork/catalog_originals.py`, `datasrc/gfx/CATALOG.md` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/building-map.md` | `datasrc/gfx/recovered-runtime.md`, `datasrc/gfx/originals/buildings/barracks/level-1/__building5.xcf`, `datasrc/gfx/originals/buildings/barracks/level-3/__building12.xcf`, `datasrc/gfx/originals/buildings/barracks/level-3/__building19.xcf` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/buildings/swarm-trellis.md` | `tools/skins/export_swarm.py` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/coverage.md` | `datasrc/gfx/building-map.md`, `datasrc/gfx/recovered-runtime.md` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/production/README.md` | `tools/artwork/package_runtime.py`, `tools/package_assets.py`, `tools/artwork/terrain_materials.py`, `tools/artwork/ai/README.md` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/production/pack-metadata/README.md` | `tools/artwork/package_runtime.py`, `tools/package_assets.py` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/recovered-runtime.md` | `tools/artwork/import_more_originals.py`, `tools/artwork/export_recovered.py`, `tools/artwork/package_runtime.py`, `tools/artwork/validate_recovered.py` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `datasrc/gfx/trail/README.md` | `tools/package_assets.py`, `tools/artwork/export_trail.py`, `tools/terrain_borders.py`, `tools/artwork/validate_trail.py` | Source-preservation/recipe record retained; original provenance inventory anchors checked. |
| `docs/ai/README.md` | `src/ai/AI.h`, `src/ai/AI.cpp`, `tools/cortex-ml/README.md` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/architecture/building-gradient-depth-model.md` | `src/map/gradient/BuildingGradientDepthPolicy.h`, `src/map/gradient/MapGradientBuilding.cpp`, `tools/gradient_depth_fit.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/architecture/win-probability-model.md` | `src/team/stats/WinProbability.cpp`, `src/team/stats/WinProbabilityModel.h`, `tools/win_probability_model.py`, `tools/conditional_logit.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/development.md` | `src/ai/AIImplementation.h`, `src/ai/AI.cpp`, `src/ai/engine/AIDecision.h`, `src/ai/observation/AIQueries.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/engine-mechanics.md` | `src/team/TeamRouting.cpp`, `src/unit/Unit.cpp`, `src/building/Services.cpp`, `src/unit/UnitActivity.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/gameplay-statistics.md` | `src/team/stats/TeamStat.cpp`, `src/team/stats/MetricCatalog.cpp`, `src/team/stats/MetricSeries.cpp`, `src/team/stats/TeamLabourStatsTest.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/README.md` | `src/ai/maxima/AIMaxima.cpp`, `src/ai/maxima/AIMaximaState.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/combat.md` | `src/ai/maxima/AIMaximaCombat.cpp`, `src/ai/maxima/AIMaximaRecon.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/configuration.md` | `src/ai/maxima/AIMaximaStrategy.h`, `src/ai/maxima/MaximaStrategyConfigTest.py`, `src/ai/maxima/MaximaStrategyDump.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/economy.md` | `src/ai/maxima/AIMaxima.cpp`, `src/ai/maxima/AIMaximaStaffingControl.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/farming.md` | `src/ai/maxima/AIMaximaFarming.cpp`, `src/ai/maxima/AIMaximaFarmingPolicy.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/maxima/food.md` | `src/ai/maxima/AIMaximaFoodLedger.cpp`, `src/ai/maxima/AIMaximaFoodSupply.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/ai/ratings.md` | `src/map/generator/core/GeneratorRegistry.cpp` | Fixed cohort measurements retained with immutable evidence links; not presented as current-controller recalibration. |
| `docs/ai/telemetry.md` | `src/ai/telemetry/AITelemetry.h`, `src/ai/telemetry/AITelemetry.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/README.md` | `datasrc/gfx/README.md`, `tools/unit-animation/README.md`, `tools/artwork/ai/README.md` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/artwork-workflow.md` | `tools/artwork/package_runtime.py`, `tools/artwork/validate_runtime.py`, `tools/artwork/render_authored.py`, `tools/artwork/runtime_provenance.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/content-quickstart.md` | `src/map/generator/core/GeneratorRegistry.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/custom-map-artwork.md` | `src/render/terrain/TerrainMaterials.cpp`, `data/terrain/tileset.json` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/high-resolution/ASSET-PROVENANCE.md` | `tools/artwork/runtime_provenance.py`, `data/highres/v1/manifest.json`, `tools/artwork/render_authored.py`, `tools/artwork/paint_landscape.py` | Generated inventory retained; source hashes/current output checked by its generator where stated. |
| `docs/assets/high-resolution/README.md` | `tools/artwork/package_runtime.py`, `data/highres/v1/manifest.json`, `tools/artwork/validate_runtime.py`, `tools/artwork/terrain_synth.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/music-packages.md` | `src/audio/MusicLibrary.cpp`, `platform/packages/protocol/src/music.ts`, `tools/encode_music.py`, `tools/music/build_web.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/music-pipeline.md` | `src/audio/SoundMixer.cpp`, `tools/music/qa/corpus.toml`, `tools/encode_music.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/music-style-guide.md` | `tools/music/sets`, `data/zik`, `src/audio/SoundMixer.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/source-attribution.md` | `data/highres/v1/manifest.json`, `datasrc/gfx/provenance/catalog.json`, `tools/terrain_builtin_names.json`, `tools/artwork/terrain_synth.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/terrain-materials.md` | `data/terrain/tileset.json`, `src/map/TerrainTypeTable.h`, `tools/terrain_builtin_names.json`, `tools/terrain_tileset.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/terrain-production.md` | `tools/artwork/material_tiles.py`, `tools/package_assets.py`, `tools/artwork/terrain_synth.py`, `tools/artwork/export_material.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/assets/terrain-rendering.md` | `tools/terrain_profile_curves.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/README.md` | `docs/README.md` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/building-authoring.md` | `src/building/types/BuildingCatalog.cpp`, `platform/packages/protocol/src/buildings.ts` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/building-catalogs.md` | `src/building/types/BuildingCatalog.cpp`, `platform/packages/protocol/src/buildings.ts` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/building-family-packages.md` | `platform/packages/protocol/src/buildings.ts` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/building-semantics.md` | `src/building/types/BuildingCatalog.cpp`, `platform/packages/protocol/src/buildings.ts` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/custom-game-setup/README.md` | `src/game/rules/CustomGameRules.cpp`, `src/game/screens/CustomGameSetupHarness.cpp`, `data/rulesets.json`, `src/ai/AIRules.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/experimental-features.md` | `src/game/ExperimentalFeatures.cpp`, `src/engine/EngineInit.cpp`, `src/app/FileFormatVersions.h`, `src/map/TerrainGroup.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/farm-areas.md` | `src/game/orders/FarmAreaTest.cpp`, `src/map/MapResources.cpp`, `tools/artwork/render_authored.py`, `src/map/Map.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/gameplay-recording.md` | `tools/recording.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/guard-area-balancing.md` | `src/map/gradient/MapGradientArea.cpp`, `src/game/orders/GuardAreaBalanceTest.cpp`, `src/map/pathfind/MapPathfindArea.cpp`, `src/map/pathfind/MapPathfindMaterial.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/markets-v2.md` | `src/unit/MarketsV2Test.cpp`, `src/unit/MarketFetchHarness.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/pre-game-map-preview.md` | `src/map/preview/LobbyMapPreview.h`, `src/map/preview/GUIMapPreview.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/resource-catalogs.md` | `datasrc/gfx/resources/manifest.json`, `tools/artwork/paint_resources.py`, `tools/artwork/package_runtime.py`, `tools/artwork/paint_landscape.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/resource-properties.md` | `src/resource/ResourceRegistry.cpp`, `data/resources/registry.json` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/terrain-resource-sets.md` | `src/resource/ResourceRegistry.cpp`, `data/resources/registry.json` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/torus-experiment.md` | `src/render/torus/TorusGeometryTest.cpp`, `src/render/torus/TorusPickingTest.cpp`, `src/render/clouds/CloudFieldTest.cpp`, `src/render/clouds/SimplexNoise.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/features/window-resizing.md` | `libgag/src/GraphicContextResize.cpp`, `libgag/include/GraphicContext.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/README.md` | `docs/README.md` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/adding-a-generator.md` | `src/map/generator/core/GeneratorRegistry.cpp`, `tools/new_map_generator.py`, `tools/map_generator_study.py`, `tools/plot_map_generator_refactor.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/allotments.md` | `src/map/generator/generators/AllotmentsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/amphitheatre.md` | `src/map/generator/generators/AmphitheatreGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/anthill.md` | `src/map/generator/generators/AnthillGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/bajada.md` | `src/map/generator/generators/BajadaGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/bastion-keys.md` | `src/map/generator/generators/BastionKeysGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/braided-delta.md` | `src/map/generator/generators/BraidedDeltaGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/braided-river.md` | `src/map/generator/generators/BraidedRiverGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/breachable-highlands.md` | `src/map/generator/generators/BreachableHighlandsGenerator.cpp`, `tools/map_telemetry.py` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/canals.md` | `src/map/generator/generators/CanalsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/caravanserai.md` | `src/map/generator/generators/CaravanseraiGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/carousel.md` | `src/map/generator/generators/CarouselGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/catalog.md` | `src/map/generator/core/GeneratorRegistry.cpp`, `src/map/generator/generators/ConcreteIslandsGenerator.cpp`, `src/map/generator/generators/ContestedCommonsGenerator.cpp`, `src/map/generator/generators/CraterLakesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/central-quarry.md` | `src/map/generator/generators/CentralQuarryGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/city-states.md` | `src/map/generator/generators/CityStatesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/cli.md` | `src/app/cli/MapCommand.cpp`, `src/map/TerrainTypeTable.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/comb.md` | `src/map/generator/generators/CombGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/constraint-search.md` | `src/map/generator/shared/Solve.h`, `src/map/generator/shared/Rivers.h`, `tools/map_generation_study.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/continents.md` | `src/map/generator/generators/ContinentsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/coral.md` | `src/map/generator/generators/CoralGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/drowned-forest.md` | `src/map/generator/generators/DrownedForestGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/drumlin-field.md` | `src/map/generator/generators/DrumlinFieldGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/emoji/design.md` | `src/map/generator/core/GeneratorRegistry.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/encircled-kingdom.md` | `src/map/generator/generators/EncircledKingdomGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/even-ground.md` | `src/map/generator/generators/EvenGroundGenerator.cpp`, `src/map/generator/shared/Solve.h` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/everglades.md` | `src/map/generator/generators/EvergladesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/fairness-model.md` | `tools/fairness_model.py`, `src/map/generator/shared/FairnessModel.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/fairness-tournament.md` | `tools/map_fairness_tournament.py`, `tools/colony_start_metrics.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/faulted-city.md` | `src/map/generator/generators/FaultedCityGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/fingerprint.md` | `src/map/generator/generators/FingerprintGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/fjord-continent.md` | `src/map/generator/generators/FjordContinentGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/forts.md` | `src/map/generator/generators/FortsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/fractal-maps.md` | `src/map/generator/generators/SierpinskiGardensGenerator.cpp`, `src/map/generator/generators/HilbertRiverGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/game-rules-for-map-design.md` | `src/map/MapResources.cpp`, `src/resource/ResourceRegistry.cpp`, `src/map/pathfind/MapPathfindMaterial.cpp`, `src/map/MapStep.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/gauntlet.md` | `src/map/generator/generators/GauntletGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/hedgerow-country.md` | `src/map/generator/generators/HedgerowCountryGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/hidden-oasis.md` | `src/map/generator/generators/HiddenOasisGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/hills.md` | `tools/map_telemetry.py`, `src/map/generator/generators/HillsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/honeycomb-isle.md` | `src/map/generator/generators/HoneycombIsleGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/hungry-marches.md` | `src/map/generator/generators/HungryMarchesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/javascript.md` | `tools/map-generators/package.py`, `tools/map_generator_study.py`, `data/generators/toolkit.d.ts`, `src/map/generator/javascript/ToolkitCoverage.json` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/karst-towers.md` | `src/map/generator/generators/KarstTowersGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/last-treeline.md` | `src/map/generator/generators/LastTreelineGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/lava-shield.md` | `src/map/generator/generators/LavaShieldGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/locust.md` | `src/map/generator/generators/LocustGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/map-generator-framework.md` | `src/map/generator/core/GenerationService.cpp`, `src/map/generator/core/GeneratorControls.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/marchland.md` | `src/map/generator/generators/MarchlandGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/maze.md` | `src/map/generator/generators/MazeGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/old-growth.md` | `src/map/generator/generators/OldGrowthGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/old-town.md` | `src/map/generator/generators/OldTownGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/orchard-commons.md` | `src/map/generator/generators/OrchardCommonsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/plantations.md` | `src/map/generator/generators/PlantationsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/polder.md` | `src/map/generator/generators/PolderGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/portage-lakes.md` | `src/map/generator/generators/PortageLakesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/rain-shadow.md` | `src/map/generator/generators/RainShadowGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/recursive-verification.md` | `src/map/generator/MapGeneratorDefaultsTest.cpp`, `tools/fractal_maps/plan.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/repeat-maps.md` | `src/map/generator/core/GeneratorRegistry.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/report-format.md` | `src/map/tools/MapReport.cpp`, `src/app/cli/MapCommand.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/resources-and-starts.md` | `src/map/generator/shared/Resources.cpp`, `src/map/generator/shared/Pipeline.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/rice-terraces.md` | `src/map/generator/generators/RiceTerracesGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/ring-world.md` | `src/map/generator/generators/RingWorldGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/savannah.md` | `src/map/generator/generators/SavannahGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/spider-web.md` | `src/map/generator/generators/SpiderWebGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/stone-highlands.md` | `src/map/generator/generators/StoneHighlandsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/switchbacks.md` | `src/map/generator/generators/SwitchbacksGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/symmetric-arena.md` | `src/map/generator/generators/SymmetricArenaGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/telemetry.md` | `tools/map_telemetry.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/the-glacis.md` | `src/map/generator/generators/GlacisGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/tidal-flats.md` | `src/map/generator/generators/TidalFlatsGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/toolkit.md` | `src/map/generator/shared`, `tools/farm_row_fit.py`, `tools/world_atlas.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/verification.md` | `src/map/generator/MapGeneratorGoldenTest.cpp`, `src/map/generator/MapGeneratorDefaultsTest.cpp`, `src/map/generator/MapGeneratorStudy.cpp`, `src/map/generator/MapGeneratorToolkitChecks.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `docs/map-generators/watershed.md` | `src/map/generator/generators/WatershedGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/who-ate-the-map.md` | `src/map/generator/generators/WhoAteTheMapGenerator.cpp` | Construction/control/validator source linked; dated run histories removed where present. Existing design/support claims retained. |
| `docs/map-generators/world-atlas.md` | `tools/world_atlas.py`, `src/map/generator/shared/WorldAtlas.h`, `src/map/generator/shared/WorldAtlasData.cpp` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `test/maxima/README.md` | `test/tests.py`, `src/ai/maxima/MaximaStrategyConfigTest.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `tools/artwork/ai/README.md` | `tools/artwork/ai/upscale.py`, `tools/artwork/package_runtime.py`, `tools/package_assets.py`, `tools/artwork/validate_runtime.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `tools/cortex-ml/README.md` | `tools/cortex-ml/dataset.py`, `src/ai/cortex/CortexPolicy.cpp`, `src/ai/cortex/CortexConstants.h`, `tools/cortex-ml/train_bc.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `tools/cortex-ml-infer/format.md` | `src/ai/cortex/CortexNet.cpp`, `tools/cortex-ml-infer/quantize.py`, `src/ai/cortex/CortexConstants.h` | Current source locations/interfaces anchored; no new runtime acceptance claim. |
| `tools/unit-animation/README.md` | `tools/unit-animation/render.py`, `tools/unit-animation/test_render.py`, `src/unit/render/UnitAnimationTest.cpp`, `tools/unit-animation/render-jobs.py` | Current source locations/interfaces anchored; no new runtime acceptance claim. |

## Final checks

- `PYTHONPATH=artifacts/docs-runtime python3 tools/check_docs.py`: 300 Markdown documents, 0 errors, 67 external links inventoried (network reachability is separate). Fixed seven section anchors left by splitting terrain/resource references.
- `python3 tools/artwork/runtime_provenance.py --check`: PASS complete recipe classification, source/output hashes and current inventory.
- `python3 -m py_compile tools/artwork/runtime_provenance.py`: passed.
- Source count assertion: 68 registered generator definition calls indexed by the catalog; all have links.
- No runtime assets, simulation behavior or save formats changed. No native builds or gameplay/platform acceptance runs were performed for this editorial change.


## Late integration review

- `tools/cortex-ml-infer/FORMAT.md` → `format.md` uses a recorded temporary-hop Git rename. Entire reference rewritten against `CortexNet.{h,cpp}`, `quantize.py`, `int_ref.py`, `CortexPolicy{,Economy}.cpp`, `CortexTypes.h`, `AICortex.cpp` and `CortexNetCoverageTest.cpp`: binary v1/Q16 header and dimensions, rounding, accumulator limits, worker/decision masks, current model selection and save persistence. Removed nonexistent Rust/parity/pilot references. Observation version 23 and action version 14 are source current; those POD versions do not imply a model schema migration.
- `tools/cortex-ml/training.md` added from `dataset.py`, `decide_dataset.py`, `reward.py`, `decide_reward.py`, BC/AWR/CQL trainers and current `CortexPolicy.cpp`. Preserves per-file transition joins, worker shaping, own-colony decision potential and terminal label semantics. Calls out the existing 19-candidate runtime versus 18-class model/trainer boundary, including class-18 labels not automatically filtered. No training pipeline/model behavior changed.
- All `src/ai/cortex` stale documentation pointers migrated in comments to maintained guides or implementation. Added maintained food-protection explanation to `docs/ai/engine-mechanics.md`, reviewed against `CortexFoodSources.{h,cpp}`, `CortexConstants.h`, `MapGradientMaterial.cpp`, `MapStep.cpp` and `AICortex.cpp`.
- `tools/unit-animation/README.md` and `docs/assets/high-resolution/README.md` now distinguish sprite pose count from rendering cadence; normal simulation rate is 30 TPS from `src/engine/EngineTiming.h`.
- Inline dated revision narratives/user quotes in landscape/framework pages rewritten as current behavior and rationale. Removed historical tournament tables/cohorts from generator guides while preserving algorithm budgets, crossing controls, town/crop isolation, grant/AI limitations and verification guidance. Kept preview revision provenance in Lava shield and archival evidence links explicitly marked historical. `fairness-tournament.md` retains adjudication/statistical definitions and current cost-estimation guidance; removed obsolete measured budget table and corrected 90,000 ticks to 50 game minutes at 30 TPS.
- Final checks: 304 Markdown docs, zero errors, 67 external links; artwork provenance `--check` PASS; Cortex Python compileall PASS. Executable Python AST unchanged after stripping docstrings across all Cortex ML/infer Python modules; comment/whitespace-stripped C++ tokens unchanged across all changed Cortex source/header files. No native simulation/build test was needed for source-comment-only edits; no corpus retraining, gameplay remeasurement or new cross-platform inference claim made.

| Additional retained page | Source review |
| --- | --- |
| tools/cortex-ml/training.md | dataset/decide_dataset schemas; reward/decide_reward; BC/AWR/CQL modules; CortexPolicy/PolicyEconomy; AICortexDebug |
| tools/unit-animation/README.md | render.py/build_layers.py plus EngineTiming.h for cadence correction |
