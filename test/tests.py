"""Registry of every native test and test-adjacent program.

test/SConscript builds two doctest binaries from these tables and scons/mobile_build.py
cross-compiles the same lists. Paths are relative to test/ unless they start with '#',
which is the repository root.

Placement rule: a test joins UNIT_TESTS if it needs neither GlobalContainer nor any
client object; otherwise it joins ENGINE_TESTS. Entry options:
  cxxflags  extra compiler flags for that translation unit only
  defines   extra preprocessor defines for that translation unit only
  require   build features the entry needs: 'wss', 'not-mingw', 'opengl'
"""

# Linked into both test binaries.
SUPPORT = [
    'support/TestMain.cpp',
    'support/Glob2Test.cpp',
    'support/GlobalContainerSlot.cpp',
]

# Linked into glob2-engine-tests only.
ENGINE_SUPPORT = [
    'support/EngineFixtures.cpp',
]

# glob2-engine-tests: every client object except the entry point, plus these.
ENGINE_TESTS = [
]

# glob2-unit-tests: libgag, libusl, the production sources below and stubs.
UNIT_TESTS = [
    # Former CppUnit suite (TestsRunner).
    'AllyTeamWidgetIndexTest.cpp',
    'BitArrayTest.cpp',
    'BrushAccumulatorTest.cpp',
    'BrushToolHitTest.cpp',
    'BuildingFailureDisplayTest.cpp',
    'CortexUpgradeTest.cpp',
    'EditorWidgetLayoutTest.cpp',
    'FertilityFieldTest.cpp',
    'FetchApportionmentTest.cpp',
    'FilenameStripTest.cpp',
    'GameMusicControllerTest.cpp',
    'GhostBuildingOverlapTest.cpp',
    'GradientBFSTest.cpp',
    'GradientTest.cpp',
    'HelloWorldTest.cpp',
    'KeyActionLookupTest.cpp',
    'MapQueryTest.cpp',
    'MessageRecipientsTest.cpp',
    'OverlayFillTest.cpp',
    'PanelButtonHitTest.cpp',
    'ParticleCrossfadeTest.cpp',
    'PerlinNoiseTest.cpp',
    'PlayerVoiceDrainTest.cpp',
    'SpriteCenteringTest.cpp',
    'TurretScanTileTest.cpp',
    'UnitAnimationTest.cpp',
    'UnitDrawGeometryTest.cpp',
    'UnitTimingTest.cpp',
    'natsort/NatSortTest.cpp',
    # Former standalone programs compiled by test/SConstruct or directly in CI.
    'BasePlayerSaveLoadTest.cpp',
    'BaseTeamSaveLoadTest.cpp',
    'BulletSaveLoadTest.cpp',
    'CloudFieldTest.cpp',
    'GameHeaderDefaultAlliancesTest.cpp',
    'GameHeaderTextSaveLoadTest.cpp',
    'GameHintsTest.cpp',
    'GameObjectivesTest.cpp',
    'LocalTimeTest.cpp',
    'MapExploredAreaSaveLoadTest.cpp',
    'MapRenderGeometryTest.cpp',
    'MersenneTwisterTest.cpp',
    'NetGamePlayerManagerTest.cpp',
    'RuntimeBuildingOrderSaveLoadTest.cpp',
    'TorusGeometryTest.cpp',
    'TorusPickingTest.cpp',
    'TriboolTest.cpp',
    'WinningConditionDecodeTest.cpp',
    'WinningConditionPrestigeToggleTest.cpp',
    'WinningConditionSuddenDeathToggleTest.cpp',
    'NetSendOrderDecodeTest.cpp',
    'OrderAlterateAreaTest.cpp',
    'ReplayStepCounterTest.cpp',
    'CampaignBoundsHarness.cpp',
    'CampaignDescriptionCacheHarness.cpp',
    'CampaignLoadHarness.cpp',
    'CampaignSelectionHarness.cpp',
    'WinningConditionsHarness.cpp',
    'BufferedFileStreamHarness.cpp',
    'ComputeExecutorHarness.cpp',
    'GradientPipelineHarness.cpp',
    ('MobileCertificateHarness.cpp', dict(require={'wss'})),
    'MobileDocumentsHarness.cpp',
    'MobileInputHarness.cpp',
    ('MobileTemporaryFilesHarness.cpp', dict(require={'not-mingw'})),
    'PerformanceTelemetryHarness.cpp',
    'ScreenExecutionHarness.cpp',
    'SoundMixerTrackSelectionHarness.cpp',
    'UILayoutHarness.cpp',
]

# Production sources the unit binary links. Plain entries reuse the client build's
# object; entries with `defines` are compiled again for the unit binary only.
UNIT_PRODUCTION_SOURCES = [
    '#src/BitArray.cpp',
    '#src/Brush.cpp',
    '#src/OverlayFill.cpp',
    '#src/PlayerVoice.cpp',
    '#src/Utilities.cpp',
    '#src/ai/shared_runtime/GradientBFS.cpp',
    '#src/building/BuildingUtils.cpp',
    '#src/gui/GameGUIKeyActions.cpp',
    '#src/gui/GameMusicController.cpp',
    '#src/map/FertilityField.cpp',
    '#src/map/Map.cpp',
    '#src/map/MapQuery.cpp',
    '#src/map/MapTerrain.cpp',
    '#src/map/edit/MapEditKeyActions.cpp',
    '#src/map/generator/shared/Noise.cpp',
    '#src/map/gradient/BuildingGradientSearch.cpp',
    '#src/map/gradient/MapGradientDirection.cpp',
    '#src/map/gradient/MapGradientPropagation.cpp',
    '#src/net/message/MessageRecipients.cpp',
    '#src/unit/UnitUtils.cpp',
    '#src/BasePlayer.cpp',
    '#src/BaseTeam.cpp',
    '#src/Bullet.cpp',
    '#src/GameHeader.cpp',
    '#src/GameHints.cpp',
    '#src/GameObjectives.cpp',
    '#src/SimplexNoise.cpp',
    '#src/WinningConditions.cpp',
    '#src/ai/shared_runtime/BuildingOrder.cpp',
    '#src/map/io/MapExploredAreaIO.cpp',
    '#src/net/NetGamePlayerManager.cpp',
    '#src/net/NetReteamingInformation.cpp',
    '#src/Campaign.cpp',
    '#src/SoundMixer.cpp',
    '#mobile/Documents.cpp',
    ('#mobile/CertificateTrust.cpp', dict(require={'wss'})),
    ('#mobile/TemporaryFiles.cpp', dict(require={'not-mingw'})),
    '#src/ReplayReader.cpp',
    '#src/ReplayWriter.cpp',
    # Without the client's Brush and Game surfaces: only the byte marshalers are wanted.
    ('#src/OrderModify.cpp', dict(defines=['YOG_SERVER_ONLY'])),
    ('#src/net/message/OrderMessages.cpp', dict(defines=['YOG_SERVER_ONLY'])),
]

# Replacement definitions for production symbols the unit binary does not link.
UNIT_STUBS = [
    'unit/stubs/Sha1.cpp',
    'unit/stubs/MapSectorStubs.cpp',
    'unit/stubs/MapHeaderStubs.cpp',
    'unit/stubs/OrderStubs.cpp',
    'unit/stubs/GameGUIStubs.cpp',
    'unit/stubs/RaceStubs.cpp',
    'unit/stubs/RuntimeStubs.cpp',
]

# Standalone programs: (target, source, alias, group). group 'test' programs are run by
# CI through their own runners; 'tools' are developer utilities and benchmarks.
PROGRAMS = [
]

# Old per-harness alias -> test binary. Kept for one release so documented commands and
# shell history keep working; delete after that.
LEGACY_ALIASES = {
    'buffered-file-test': 'glob2-unit-tests',
    'compute-executor-test': 'glob2-unit-tests',
    'gradient-pipeline-test': 'glob2-unit-tests',
    'mobile-certificate-test': 'glob2-unit-tests',
    'mobile-documents-test': 'glob2-unit-tests',
    'mobile-input-test': 'glob2-unit-tests',
    'mobile-temporary-files-test': 'glob2-unit-tests',
    'performance-telemetry-test': 'glob2-unit-tests',
    'screen-test': 'glob2-unit-tests',
    'ui-layout-test': 'glob2-unit-tests',
}
