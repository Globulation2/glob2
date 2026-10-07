from pathlib import Path
exec(compile(Path('artifacts/unit-rigs/worker-full-ablation/constrained_finish.py').read_text().split('unrestricted=s.best')[0],'constrained_setup','exec'))
center=s.best['parameters'].copy()
# The detected collisions are between opposite shaft/collar rings, not caps.
# Test clearance with shaft girth and socket aperture as coupled controls.
for clearance,shaft,socket in itertools.product([50.,55.],[1.1,1.3,1.5],[.5,.65]):
 p=center.copy();p.update(clearance=clearance,shaft=shaft,socket=socket)
 s.trial(p,f'socket-clearance-{clearance}-{shaft}-{socket}')
if s.best['parameters']['clearance']<60:
 for name in ['body','end','bulb','shaft','depth','extension','socket','collar','attachment','clearance','curl']:
  center=s.best['parameters'].copy()
  for sign in [-1,1]:
   p=center.copy();p[name]=round(float(np.clip(center[name]+sign*s.step[name]*.25,*s.bounds[name])),5);s.trial(p,f'final-coupled-fit-{name}-{sign}')
print('SOCKET SEARCH COMPLETE',s.best['id'],s.best['mean'],flush=True)
