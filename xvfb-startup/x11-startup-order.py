import subprocess,time,shlex,json,sys
from pathlib import Path
ready=Path(sys.argv[1])
startup=shlex.join([sys.executable,'-c','from pathlib import Path; import sys; Path(sys.argv[1]).touch()',str(ready)])
wm=subprocess.Popen(['openbox','--sm-disable','--startup',startup],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
t0=time.monotonic();published=None; early=False
try:
 while time.monotonic()-t0<5:
  state=subprocess.run(['xprop','-root','_NET_SUPPORTING_WM_CHECK'],capture_output=True,text=True)
  if 'window id #' in state.stdout and published is None:published=time.monotonic()-t0
  if published is not None and not ready.exists():early=True
  if ready.exists():print(json.dumps({'ewmhSeconds':published,'startupCallbackSeconds':time.monotonic()-t0,'observedIdentityBeforeCallback':early}),flush=True);break
 else:raise RuntimeError('Openbox startup timeout')
finally:
 wm.terminate();wm.wait(timeout=5)
