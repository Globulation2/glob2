#!/usr/bin/env python3
"""Reuse corrected primary build evidence: Oazis never rebuilds frozen binaries."""
from pathlib import Path
import os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parent
PRIMARY=ROOT.with_name('engine-history')
os.sched_setaffinity(0,{15,31})
subprocess.run([sys.executable,str(PRIMARY/'plot_build_times.py')],check=True)
for extension in ['png','svg','csv','json']:
    shutil.copyfile(PRIMARY/('build-times.'+extension),ROOT/('build-times.'+extension))
print('Oazis reuses primary frozen binaries and corrected build evidence; no additional builds.')
