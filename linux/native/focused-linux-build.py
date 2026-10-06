import importlib.util,sys
from pathlib import Path
root=Path.cwd()
spec=importlib.util.spec_from_file_location("tests",root/"test/tests.py")
registry=importlib.util.module_from_spec(spec);sys.modules["tests"]=registry;spec.loader.exec_module(registry)
engine=("MarketFetchHarness.cpp","MarketsV2Test.cpp","BuildingCatalogTest.cpp","GradientPreparationTest.cpp","BuildingGradientInvalidationHarness.cpp","MapGradientInvalidationTest.cpp","ClearingFlagGradientTest.cpp","ImmobileUnitGradientHarness.cpp","MatchSetupTest.cpp","GameHeaderTextSaveLoadTest.cpp","TerrainPropertiesTest.cpp","TerrainRuntimeTest.cpp","ReadOnlyPhaseTest.cpp","SimulationReadPhaseTest.cpp","TurnEngineHarness.cpp","MaximaEconomyRegressionTest.cpp","AIOrderSchedulerTest.cpp","AIPipelineTest.cpp","WorldSnapshotTest.cpp","NumbiObservationTest.cpp","WarrushObservationTest.cpp","AIStateContinuationTest.cpp","SharedWorkerLifecycleTest.cpp","CastorContinuationTest.cpp","AIRulesTest.cpp","RuntimeBuildingOrderSaveLoadTest.cpp")
unit=("ExperimentalFeaturesTest.cpp","GameHeaderDefaultAlliancesTest.cpp","ReplayStepCounterTest.cpp","MobileDocumentsHarness.cpp","MobileCertificateHarness.cpp","GameHeaderTextSaveLoadTest.cpp","GradientTest.cpp","PathGradientHarness.cpp","GradientPipelineHarness.cpp","ComputeExecutorHarness.cpp","PerformanceTelemetryHarness.cpp","MaximaFoodLedgerStandaloneTest.cpp")
def path(e):return e[0] if isinstance(e,tuple) else e
registry.ENGINE_TESTS=[e for e in registry.ENGINE_TESTS if path(e).endswith(engine)]
registry.UNIT_TESTS=[e for e in registry.UNIT_TESTS if path(e).endswith(unit)]
print("Selected:",registry.ENGINE_TESTS,registry.UNIT_TESTS,flush=True)
sys.argv=["scons","-j4","release=1","tests","build/linux/client/release/src/glob2"]
import SCons.Script
SCons.Script.main()
