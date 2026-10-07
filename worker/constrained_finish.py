from pathlib import Path
import importlib.util,json,struct,itertools
import numpy as np
path=Path('artifacts/unit-rigs/worker-full-ablation/search.py').resolve();spec=importlib.util.spec_from_file_location('search',path);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
from limb_surface import LimbSurface
surface=LimbSurface(json.loads(Path('datasrc/gfx/authored/skins/limb-surfaces.json').read_text())['worker']);body=np.array([i for i,d in enumerate(surface.vertices) if d[0]=='body'])
def aspect(folder):
 d=(folder/'worker-walk.gsr').read_bytes();n=struct.unpack_from('<I',d,4)[0];p=np.ndarray((n,3),dtype='<f4',buffer=d,offset=28,strides=(64,4));size=np.ptp(p[body],axis=0);return float(size[2]/size[1])
old_validate=s.validate
def validate(folder):
 ratio=aspect(folder)
 if ratio<=1.4:
  result=dict(samplesChecked=0,torsoAspect=ratio,constraint='Tall torso: z/y > 1.4');(folder/'validation.json').write_text(json.dumps(result,indent=2));return False,result
 valid,result=old_validate(folder);result['torsoAspect']=ratio;return valid,result
s.validate=validate
unrestricted=s.best['parameters'].copy();(s.root/'overlap-only-best.json').write_text(json.dumps(s.best,indent=2))
for row in s.results:
 if 'mean' not in row:continue
 ratio=aspect(s.root/row['id']);row['torsoAspect']=ratio
 if ratio<=1.4:row['valid']=False;row['shapeConstraint']='Tall torso: z/y > 1.4'
s.best=None
for row in sorted((r for r in s.results if 'mean' in r),key=lambda r:r['mean'],reverse=True):
 if row.get('valid') is False:continue
 if row.get('valid') is None:row['valid'],row['validation']=validate(s.root/row['id'])
 if row['valid']:s.best=row;break
assert s.best
(s.root/'results.json').write_text(json.dumps(s.results,indent=2));(s.root/'best.json').write_text(json.dumps(s.best,indent=2))
for extension in [1.15,1.2,1.25,1.3]:
 p=unrestricted.copy();p['extension']=extension;s.trial(p,f'tall-torso-repair-{extension}')
center=s.best['parameters'].copy()
for clearance,bulb,swing in itertools.product([50.,55.],[1.7,1.9,2.1],[.75,.9]):
 p=center.copy();p.update(clearance=clearance,bulb=bulb,swing=swing);s.trial(p,f'clearance-terminal-{clearance}-{bulb}-{swing}')
for name in s.base:
 center=s.best['parameters'].copy()
 for sign in [-1,1]:
  p=center.copy();p[name]=float(np.clip(center[name]+sign*s.step[name]*.25,*s.bounds[name]));p[name]=int(round(p[name])) if name=='fairing' else round(p[name],5);s.trial(p,f'constrained-fine-{name}-{sign}')
print('CONSTRAINED COMPLETE',s.best['id'],s.best['mean'],flush=True)
