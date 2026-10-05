import importlib.util, subprocess, unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location("package_test", "test/build_system/test_browser_package.py")
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
base=subprocess.check_output(["git", "show", "2a775e0aa7a1830b9d2cac518833ad27da94fa2f:.github/workflows/build.yml"],text=True)
case=m.BrowserPackageTests("test_ci_artifacts_publish_the_complete_music_runtime")
with patch.object(Path, "read_text", return_value=base):
    result=unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite([case]))
assert len(result.failures)==2 and not result.errors
assert all("music-worker.js" in detail for _,detail in result.failures)
print("PASS: regression test rejects both original incomplete upload inventories.")
