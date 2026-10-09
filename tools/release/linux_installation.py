#!/usr/bin/env python3
"""Write installation guidance and actual linked libraries into a Linux archive."""
import argparse
from pathlib import Path
import re
import subprocess


def write(stage):
    executable=stage/'usr/bin/glob2'
    output=subprocess.check_output(['ldd',str(executable)],text=True)
    if 'not found' in output: raise ValueError('Packaged Linux executable has unresolved system libraries')
    libraries=sorted(set(re.findall(r'^\s*(\S+)\s+=>',output,re.M)))
    if not libraries: raise ValueError('Could not identify the Linux system-library dependencies')
    (stage/'INSTALL.txt').write_text('''Globulation 2 — Ubuntu 22.04 x86-64 installation archive

Prefer the Flatpak or Snap when you want an isolated runtime.
This archive needs system libraries from Ubuntu 22.04 or a compatible system.
Libraries included under usr/lib/glob2 are shipped in this archive.
Install the remaining libraries listed below with your distribution package manager.

To install the executable and shared assets after extracting the archive:
  sudo cp -a usr/bin/glob2 /usr/bin/glob2
  sudo cp -a usr/share/glob2 /usr/share/glob2
  sudo mkdir -p /usr/lib/glob2
  sudo cp -a usr/lib/glob2/. /usr/lib/glob2/
Then run:
  glob2

User saves and settings remain in the application user-data directory.
Uninstall only /usr/bin/glob2, /usr/share/glob2 and /usr/lib/glob2; preserve user data.
Do not overwrite an installation managed by your distribution package manager.

Required linked library filenames (recorded from the final staged executable):
'''+''.join('  '+name+'\n' for name in libraries))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    write(parser.parse_args().stage)
