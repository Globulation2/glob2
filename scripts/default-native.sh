#!/bin/sh
set -eu
python3 test/run_tests.py \
 --filter 'TurnEngineHarness/the committed*' \
 --filter 'RoundTripHungerGate/*' --filter 'ResourceFetchTarget/*' \
 --filter 'InnSwap/*' --filter 'MarketFetch/*' --filter 'MarketsV2/*' \
 --filter 'MatchSetup/*' --filter 'GameHeaderTextSaveLoad/*' \
 --filter 'ExperimentalFeatures/*' --filter 'ReplayStepCounter/*' \
 --filter 'GradientPreparation/*' --filter 'BuildingGradientInvalidation/*' \
 --filter 'MapGradientInvalidation/*' --filter 'ClearingFlagGradient/*' \
 --filter 'ImmobileUnitGradient/*' --filter 'GradientPipeline/*' \
 --filter 'RuntimeBuildingOrderSaveLoad/*' \
 --junit artifacts/greedy-fetch/default-native.xml > artifacts/greedy-fetch/default-native.log 2>&1
