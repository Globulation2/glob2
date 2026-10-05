import importlib.util,json,os,subprocess
from pathlib import Path
r=Path.cwd();d=r/'artifacts/coverage-profile-repair/llvm';d.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('coverage',r/'test/run_coverage.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
src=d/'probe.c';src.write_text('int main(int argc,char **argv){if(argc>1)return 0;return 0;}\n')
subprocess.run(['clang-18','-O0','-fprofile-instr-generate','-fcoverage-mapping',str(src),'-o',str(d/'probe')],check=True)
for i,args in enumerate([[],['branch']]):
 subprocess.run([str(d/'probe'),*args],env=dict(os.environ,LLVM_PROFILE_FILE=str(d/f'{i}.profraw')),check=True)
raw=sorted(d.glob('*.profraw'));profile=d/'coverage.profdata'
subprocess.run(['llvm-profdata-18','merge','-sparse',*map(str,raw),'-o',str(profile)],check=True)
cmd=['llvm-cov-18','export',str(d/'probe'),f'-instr-profile={profile}']
before=subprocess.check_output(cmd)
subprocess.run(['llvm-cov-18','show',str(d/'probe'),f'-instr-profile={profile}','-format=html',f'-output-dir={d}/html'],check=True,stdout=subprocess.DEVNULL)
retention=m.compact_profiles(raw,successful=True)
after=subprocess.check_output(cmd);assert before==after;assert not any(p.exists() for p in raw)
(d/'coverage.json').write_bytes(after)
print(json.dumps({'export_identical_after_compaction':True,'retention':retention,'llvm_version':subprocess.check_output(['llvm-profdata-18','--version'],text=True).strip()},indent=2))
