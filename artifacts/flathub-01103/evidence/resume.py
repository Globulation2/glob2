import os, shlex, subprocess
from pathlib import Path
root = Path.cwd()
lines = (root / 'build.log').read_text().splitlines()
line = next(line for line in lines if line.startswith("FB: Running 'flatpak build ") and 'scons -j2 release=1' in line)
argv = shlex.split(line[len("FB: Running '"):-len("' on host")])
argv = [str(root / 'builddir') if arg.startswith(str(root / '.flatpak-builder/rofiles/rofiles-')) else arg for arg in argv]
assert argv[-2] == '-c'
assert 'scons -j2 release=1' in argv[-1]
argv[-1] = argv[-1].replace('scons -j2 release=1', 'scons -j8 release=1')
argv[0] = subprocess.check_output(['flatpak-spawn','--host','which','flatpak'],text=True).strip()
os.environ['FLATPAK_USER_DIR'] = str(Path.home() / '.local/share/flatpak')
with (root / 'resume-command.txt').open('w') as output:
    output.write(shlex.join(argv)+'\n')
code = subprocess.call(['flatpak-spawn', '--host', *argv])
if code == 0:
    code = subprocess.call(['flatpak-builder','--verbose','--finish-only','--sandbox','--keep-build-dirs','--override-source-date-epoch','1321009871','--user','--install','--mirror-screenshots-url=https://dl.flathub.org/media','--compose-url-policy=full','--repo=repo','builddir','org.globulation2.Globulation2.yml'],env=dict(os.environ,FLATPAK_BINARY=argv[0]))
(root / 'resumed-build.exit').write_text(str(code)+'\n')
raise SystemExit(code)
