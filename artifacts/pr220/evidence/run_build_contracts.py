"""Final build contracts; exclude the unavailable system FFmpeg tests explicitly."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path.cwd()))

def cases(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from cases(test)
        else:
            yield test

all_cases=list(cases(unittest.defaultTestLoader.discover('test/build_system')))
selected=[]
for test in all_cases:
    if test.id().startswith('test_music_encoding.'):
        print('EXCLUDED (FFmpeg cannot start due to missing Homebrew libjxl):',test.id(),flush=True)
    else:
        selected.append(test)
result=unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(selected))
sys.exit(0 if result.wasSuccessful() else 1)
