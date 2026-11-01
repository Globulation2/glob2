import pathlib,time,json,os
base=pathlib.Path('artifacts/marathi-localization/integration-refresh/native');owner=2672821;target='tools/TranslationPreviewHarness.cpp'
def children(pid):
 try:ids=(pathlib.Path('/proc')/str(pid)/'task'/str(pid)/'children').read_text().split()
 except OSError:return []
 out=[]
 for p in ids:out+=[int(p)]+children(int(p))
 return out
while pathlib.Path('/proc/'+str(owner)).exists():
 for pid in children(owner):
  try:args=(pathlib.Path('/proc')/str(pid)/'cmdline').read_bytes().decode().strip('\0').split('\0')
  except (OSError,UnicodeDecodeError):continue
  if args and pathlib.Path(args[0]).name in ('g++','c++','ccache') and target in args:
   (base/'actual-harness-compile-argv.json').write_text(json.dumps({'owner_scons_pid':owner,'compiler_pid':pid,'cwd':str((pathlib.Path('/proc')/str(pid)/'cwd').resolve()),'argv':args},indent=2)+'\n');print('Captured actual own harness compiler argv',pid,flush=True);raise SystemExit(0)
 time.sleep(1)
raise SystemExit('Own builder ended before compiler argv was captured; use logged compile command explicitly')
