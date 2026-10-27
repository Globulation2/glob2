"""Reproduce the narrow acceptance audit; does not change expected tables."""
import importlib.util,json,subprocess
from pathlib import Path
ROOT=Path(subprocess.check_output(['git','rev-parse','--show-toplevel'],text=True).strip())
HERE=Path(__file__).resolve().parent
OBS=HERE/'generators' if (HERE/'generators').is_dir() else HERE/'macos-generator-observations'
spec=importlib.util.spec_from_file_location('evidence',ROOT/'test/collect_generator_evidence.py')
e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)
def git_table(ref,platform):
 return e.rows(subprocess.check_output(['git','show',ref+':test/map-generator-golden.txt'],cwd=ROOT,text=True),platform)
def case(k):return k[:1]+k[2:]
old=git_table('697ad9d29ed4c2c7d5c965c71dd4ad54395e8306','macos-arm64')
new=e.rows((ROOT/'test/map-generator-golden.txt').read_text(),'macos-arm64')
observed=e.rows((OBS/'rows.txt').read_text(),'macos-arm64')
assert new==observed
for n in (0,1):assert e.rows((OBS/f'print-{n}.log').read_text(),'macos-arm64')==new
before=git_table('a45db5484^','linux-x86_64');after=git_table('a45db5484','linux-x86_64')
accepted={case(k) for k in new if k not in old}
assert accepted=={case(k) for k in after if k not in before}
assert len(accepted)==92 and len(new)==len(old)==564
assert all(new[k]==v for k,v in old.items() if k in new)
assert git_table('697ad9d29ed4c2c7d5c965c71dd4ad54395e8306','linux-x86_64')==e.rows((ROOT/'test/map-generator-golden.txt').read_text(),'linux-x86_64')
subprocess.run(['git','diff','--exit-code','d80e39b1521aeef0097df3a0e350748f33a2755f','--','src/map/generator/generators','src/map/generator/shared','src/map/generator/core'],cwd=ROOT,check=True)
print('PASS: exact 92 reviewed cases, both repeated observations, 472 current rows unchanged, all Linux rows unchanged, producer generator source unchanged')
