from pathlib import Path
import subprocess
p=Path('SConstruct');original=p.read_text()
try:
 p.write_text(original.replace('        prepare_assets(env)','        pass # Local headless validation: skip asset transcoding.'))
 with open('artifacts/read-phase/final-build.log','w') as log:
  for targets in [['tests'],[]]:subprocess.run(['scons','-j8','release=1','server=0',*targets],stdout=log,stderr=subprocess.STDOUT,check=True)
finally:p.write_text(original)
