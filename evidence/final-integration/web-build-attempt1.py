import os,subprocess,json,datetime
from pathlib import Path
O=Path(__file__).resolve().parent;cwd='/home/bradley/.codex/worktrees/engine-optimization/glob2';env=os.environ.copy()
removed={k:env.pop(k) for k in list(env) if k in ['EMSDK','EM_CONFIG','GLOB2_DEV_MODE'] or k.startswith('GLOB2_TEST')}
env.update(CCACHE='1',GLOB2_RENDER_SHADER_INPUT='/home/bradley/.codex/worktrees/execution-resource-profiles/glob2/artifacts/resource-profiles/validation/renderer-bundle-v2/shaders')
cmd=['scons','target=web','web_variant=threaded','release=1','emsdk=/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39','web-tests','build/emscripten/client/release/index.html','-j32']
r={'state':'running','command':cmd,'cwd':cwd,'env_overrides':{k:env[k] for k in ['CCACHE','GLOB2_RENDER_SHADER_INPUT']},'removed_env_keys':list(removed),'commit':'efb843d8047bba6f546c031614a31c917513ad54','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};(O/'web-build.json').write_text(json.dumps(r,indent=2)+'\n')
with(O/'web-build.log').open('w') as f:p=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
r.update(state='passed' if p.returncode==0 else 'failed',returncode=p.returncode,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());(O/'web-build.json').write_text(json.dumps(r,indent=2)+'\n');raise SystemExit(p.returncode)
