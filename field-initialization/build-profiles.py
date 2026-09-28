from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
source=root/'src/map/gradient/MapGradientBuilding.cpp'
try:
 source.write_text((out/'before.cpp').read_text())
 with (out/'build-before.log').open('w') as f:subprocess.run(['scons','-j8','release=1','server=0','build/darwin/client/release/src/glob2'],cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
 shutil.copy2(root/'build/darwin/client/release/src/glob2',out/'glob2-before')
 for version in ['before','after']:
  s=(out/(version+'.cpp')).read_text()
  s='#include "'+str(out/'InitProfile.h')+'"\n'+s
  s=s.replace('PERF_SCOPE_TIME(BuildingGradient);','PERF_SCOPE_TIME(BuildingGradient);\n\tInitProfile::Timer initTimer(building->type->isVirtual, size);',1)
  s=s.replace('std::fill(gradient, gradient+size, GRADIENT_UNREACHABLE);','{ auto start=InitProfile::Clock::now(); std::fill(gradient, gradient+size, GRADIENT_UNREACHABLE); InitProfile::metrics.fill[building->type->isVirtual]+=std::chrono::duration_cast<std::chrono::nanoseconds>(InitProfile::Clock::now()-start).count(); }',1)
  s=s.replace('\n\tif (lazy)\n','\n\tinitTimer.stop();\n\tif (lazy)\n',1)
  source.write_text(s)
  (out/(version+'-profile.cpp')).write_text(s)
  with (out/('build-'+version+'-profile.log')).open('w') as f:subprocess.run(['scons','-j8','release=1','server=0','build/darwin/client/release/src/glob2'],cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
  shutil.copy2(root/'build/darwin/client/release/src/glob2',out/('glob2-'+version+'-profile'))
 # A validation-only binary checks every initialized cell against the old path.
 s=(out/'after.cpp').read_text()
 old=(out/'before.cpp').read_text()
 start=old.index('\tbool isClearingFlag=false;')
 end=old.index('\n\tif (!building->type->isVirtual)',start)
 reference=old[start:end]
 marker='\n\tif (!building->type->isVirtual)\n\t{\n\t\t// Spiral'
 check='\n\t{\n\t\tstd::vector<Uint16> expected(size);\n\t\tUint16 *actual=gradient;\n\t\tgradient=expected.data();\n'+reference+'\n\t\tgradient=actual;\n\t\tif (!std::equal(expected.begin(), expected.end(), gradient)) { std::fprintf(stderr, "INITIALIZATION MISMATCH\\n"); std::abort(); }\n\t}\n'
 assert marker in s
 s=s.replace(marker,check+marker,1)
 source.write_text(s)
 (out/'verify.cpp').write_text(s)
 with (out/'build-verify.log').open('w') as f:subprocess.run(['scons','-j8','release=1','server=0','build/darwin/client/release/src/glob2'],cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
 shutil.copy2(root/'build/darwin/client/release/src/glob2',out/'glob2-verify')
finally:source.write_text((out/'after.cpp').read_text())
