from pathlib import Path
import subprocess,os,json,hashlib
root=Path('/Users/bradley/glob2/artifacts/castor-save-continuation')
evidence=Path('/Users/bradley/glob2/artifacts/native-coverage-implementation/castor-before-after');evidence.mkdir(exist_ok=True)
source=root/'src/ai/castor/Lifecycle.cpp';test=root/'test/CastorContinuationTest.cpp'
fixed=source.read_bytes(); original_test=test.read_bytes()
old=subprocess.check_output(['git','show','98301f7cb:src/ai/castor/Lifecycle.cpp'],cwd=root)
probe=original_test.decode().replace('#include <set>','#include <set>\n#include <fstream>\n#include <iomanip>\n#include <cstdlib>')
needle='for (int i=0; i<512; ++i) { orders.push_back(tick(initial.world.game)); traces.push_back(state(initial.world.game)); }'
assert needle in probe
probe=probe.replace(needle,'''std::ofstream trace(std::getenv("GLOB2_CASTOR_TRACE"));
            for (int i=0; i<512; ++i) {
                orders.push_back(tick(initial.world.game)); traces.push_back(state(initial.world.game));
                trace<<std::dec<<i<<" order ";
                for (unsigned char c:orders.back()) trace<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);
                trace<<" state"; for (auto c:traces.back()) trace<<" "<<std::hex<<c;
                trace<<" rng "<<std::dec<<syncRandEngine()<<"\\n";
            }''')
cmd=['scons','-j8','release=0','server=0','--build=build/coverage-audit','tests','CC=clang','CXX=clang++','CFLAGS=-g -O0 -fprofile-instr-generate -fcoverage-mapping','CXXFLAGS=-g -O0 -fprofile-instr-generate -fcoverage-mapping','LINKFLAGS=-g -fprofile-instr-generate']
try:
 test.write_text(probe)
 for label,contents in [('fixed',fixed),('master-lifecycle',old)]:
  source.write_bytes(contents)
  (evidence/(label+'-Lifecycle.cpp')).write_bytes(contents)
  with (evidence/(label+'-build.log')).open('w') as log: subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
  env=os.environ.copy();env['GLOB2_CASTOR_TRACE']=str(evidence/(label+'.trace'));env['LLVM_PROFILE_FILE']=str(evidence/(label+'-%m-%p.profraw'))
  with (evidence/(label+'-tests.log')).open('w') as log: subprocess.run(['python3','test/run_tests.py','--build-dir','build/coverage-audit','--filter','CastorContinuation/Castor seeded*','--junit',str(evidence/(label+'.xml'))],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 a=(evidence/'fixed.trace').read_bytes();b=(evidence/'master-lifecycle.trace').read_bytes()
 result={'ticks':512,'match':a==b,'fixed_sha256':hashlib.sha256(a).hexdigest(),'master_sha256':hashlib.sha256(b).hexdigest(),'scope':'Identical current harness and headers; only Castor Lifecycle.cpp replaced by master 98301f7cb. Full per-tick order bytes, simulation/entity checksum fields and RNG; MapHeader format metadata excluded by the harness.'}
 (evidence/'comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(result,flush=True)
finally:
 source.write_bytes(fixed);test.write_bytes(original_test)
