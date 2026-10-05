from pathlib import Path
import sys
from SCons.Script import *
sys.path.insert(0,str(Path.cwd() / 'scons'))
from build_layout import build_identity
from web_build import _build_variant
directory=Path('artifacts/browser-e163/profile-build')
SConsignFile(str(directory / '.profile-sconsign'))
identity=build_identity(ARGUMENTS)
env, serial, database, packaged=_build_variant(directory,identity,ARGUMENTS)
_, threaded, _, _=_build_variant(directory / 'threaded',identity,ARGUMENTS,True,packaged)
Default(threaded)
