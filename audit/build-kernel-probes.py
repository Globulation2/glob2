from pathlib import Path
import subprocess,json
root=Path.cwd();out=root/'artifacts/gradient-cpu-audit';commands=[]
for v,includes,defines in [
 ('before',[root/'artifacts/gradient-cpu-ab/before/src'],[]),
 ('runtime',[out/'original-runtime',root/'src'],['AUDIT_SCOPE']),
 ('resolved',[out/'resolved-runtime',root/'src'],['AUDIT_SCOPE']),
 ('static-sse',[out/'static-sse',root/'src'],['AUDIT_SCOPE']),
 ('hoisted',[root/'src'],['AUDIT_SCOPE','AUDIT_HOIST'])]:
 cmd=['g++','-std=c++20','-O3','-g1','-DNDEBUG',*['-I'+str(p) for p in includes],'-I'+str(root/'src/field'),*['-D'+d for d in defines],str(out/'kernel-probe.cpp'),'-o',str(out/(v+'-kernel'))]
 commands.append(cmd);subprocess.run(cmd,check=True)
(out/'kernel-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
