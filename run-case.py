import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('maxima_config', 'src/ai/maxima/MaximaStrategyConfigTest.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
case=m.MaximaStrategyConfigTest('test_player_override_changes_only_the_focal_maxima')
case.game_binary=Path('artifacts/maxima-cli2-runs/native/build/linux/client/release/src/glob2').resolve()
result=unittest.TestResult();case.run(result)
print('tests',result.testsRun,'failures',result.failures,'errors',result.errors,'skipped',result.skipped)
raise SystemExit(not result.wasSuccessful())
