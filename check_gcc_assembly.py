import json,shlex,subprocess
from pathlib import Path
out=Path(__file__).parent
query='import json; d=json.load(open("/work/build/linux/client/release/retain-symbols/dev-linker-auto/compile_commands.json"));print(json.dumps(next(x for x in d if x["file"]=="src/team/Team.cpp")))'
entry=json.loads(subprocess.check_output(['docker','exec','glob2-hiring-callgrind','python3','-c',query],text=True))
a=shlex.split(entry['command']); i=a.index('-o'); del a[i:i+2]; a.remove('-c');a.remove('src/team/Team.cpp')
a=[x.replace('\\"','"') for x in a];a+=['-pipe','-S','-o','-','-x','c++','-']
(out/'gcc-assembly-command.json').write_text(json.dumps(a,indent=2))
sources={'baseline':subprocess.check_output(['git','show','HEAD:src/team/Team.cpp'],text=True),'candidate':Path('src/team/Team.cpp').read_text()}
for label,source in sources.items():
 r=subprocess.run(['docker','exec','-i','-w','/work','glob2-hiring-callgrind',*a],input=source,text=True,capture_output=True)
 (out/(label+'-gcc.stderr')).write_text(r.stderr)
 if r.returncode:raise SystemExit(r.stderr)
 s=r.stdout; (out/(label+'-Team.s')).write_text(s)
 start=s.index('_ZN4Team9integrityEv:');end=s.index('\n\t.size\t_ZN4Team9integrityEv',start)
 (out/(label+'-integrity.s')).write_text(s[start:end])
 print(label,'compiled',len(s),'bytes',flush=True)
