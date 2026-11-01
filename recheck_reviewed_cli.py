import sys, unittest, xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'test'))
import test_cli_smoke as m
m.BINARY=str(root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2')
m.ARTIFACTS=root/'artifacts/win-probability/review-final/cli-recheck'
suite=unittest.TestSuite(m.CliSmoke(name) for name in ('test_ai_decision_profile_gate_preserves_execution','test_generated_reference_matches_binary'))
result=unittest.TextTestRunner(verbosity=2,resultclass=m.JunitResult).run(suite)
ET.ElementTree(result.report).write(root/'artifacts/win-probability/review-final/cli-recheck.xml')
raise SystemExit(not result.wasSuccessful())
