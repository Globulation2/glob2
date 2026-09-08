#!/usr/bin/env python3
"""Run the standalone regression binaries with task-local profiles and fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
directory=root/'test'
profiles=root/'build/test-profiles'
profiles.mkdir(parents=True,exist_ok=True)
programs=[directory/'TestsRunner']+sorted(set(directory.glob('*Harness'))|set(directory.glob('*Test')))
failed=[]
with tempfile.TemporaryDirectory(prefix='standalone-',dir=profiles) as temporary:
    env=dict(os.environ,TMPDIR=temporary,GLOB2_USER_DATA_DIR=temporary)
    for program in programs:
        if not program.is_file() or not os.access(program,os.X_OK):
            raise SystemExit(f'Missing executable {program}; build with scons in test/')
        print('Running',program.name,flush=True)
        if subprocess.run([str(program)],cwd=directory,env=env).returncode:
            failed.append(program.name)
if failed:
    raise SystemExit('Failed regression binaries: '+', '.join(failed))
