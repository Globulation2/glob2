import os,subprocess,time,json,pathlib,shlex,hashlib
root=pathlib.Path(__file__).resolve().parents[2]
p=root/'artifacts/ci-experiments/object-benchmark';p.mkdir(exist_ok=True)
build=root/'build/emscripten/client/release'
selected={'src/OrderMisc.cpp','libgag/src/GraphicContextDraw.cpp','src/ai/cortex/AICortex.cpp','src/map/generator/generators/PlantationsGenerator.cpp'}
commands=[]
for line in (p.parent/'wasm-cold.log').read_text().splitlines():
 if line.startswith('/Users/') and ' -c ' in line:
  cmd=shlex.split(line)
  if cmd[-1] in selected:
   output=p/(pathlib.Path(cmd[-1]).stem+'.o');cmd[cmd.index('-o')+1]=str(output)
   commands.append((cmd,output))
assert len(commands)==4
(p/'tmp').mkdir(exist_ok=True)
env=dict(os.environ,EM_COMPILER_WRAPPER='/opt/homebrew/bin/ccache',EMCC_CORES='2',
 EM_CACHE=str(build/'cache'),EM_PORTS=str(build/'ports'),TMPDIR=str(p/'tmp'),
 CCACHE_DIR=str(p/'cache'),CCACHE_COMPILERCHECK='content',CCACHE_MAXSIZE='100M')
rows=[]
for label in ('cold','warm'):
 for command,output in commands:
  output.unlink(missing_ok=True)
  start=time.monotonic()
  r=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True)
  assert r.returncode==0,r.stderr
  rows.append(dict(label=label,source=command[-1],seconds=time.monotonic()-start,sha256=hashlib.sha256(output.read_bytes()).hexdigest(),command=command))
 stats=subprocess.check_output(['ccache','--print-stats'],env=env,text=True)
 (p/(label+'-stats.txt')).write_text(stats)
for i in range(4):assert rows[i]['sha256']==rows[i+4]['sha256']
(p/'results.json').write_text(json.dumps(rows,indent=2))
print(json.dumps([{k:v for k,v in r.items() if k!='command'} for r in rows]),flush=True)
