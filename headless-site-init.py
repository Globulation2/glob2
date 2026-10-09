# Local headless verification: compiling and running simulation needs no artwork export.
import sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd() / 'scons'))
import runtime_assets
runtime_assets.prepare_assets = lambda env: None
