# Select the existing engine harness inventory without changing tracked build files.
import importlib.util
import pathlib
import sys
path = pathlib.Path.cwd() / 'test/tests.py'
spec = importlib.util.spec_from_file_location('tests', path)
registry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(registry)
registry.ENGINE_TESTS = ['#src/team/LiveSlotListTest.cpp']
sys.modules['tests'] = registry
