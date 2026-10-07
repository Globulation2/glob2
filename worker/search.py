from pathlib import Path
import sys, types, json, struct, subprocess, os, time, math, hashlib, traceback
import numpy as np
from mathutils.bvhtree import BVHTree
ROOT=Path.cwd(); root=ROOT/'artifacts/unit-rigs/worker-full-ablation'
sys.path.insert(0,str(ROOT/'tools/skins'))
S=(root/'baseline/worker_surface.py').read_text(); A=(root/'baseline/author_worker_rig.py').read_text()
base=dict(body=2.8,end=5.4,bulb=2.,shaft=1.5,depth=.85,extension=1.4,midsection=.8,socket=.5,collar=.3,fairing=24,waist_depth=.08,waist_width=.10,waist_height=1.1,attachment=.78,support_inner=.5,support_outer=1.3,anchor_inner=2.7,anchor_middle=4.4,swing=.6,clearance=60.,curl=.08,curl_response=.5)
# All values preserve the topology and reflection symmetries. No chest bulges.
bounds=dict(body=(2.4,3.4),end=(4.8,6.6),bulb=(1.5,2.4),shaft=(1.1,1.9),depth=(.65,1.15),extension=(.8,2.2),midsection=(.4,1.4),socket=(.35,.75),collar=(.15,.6),fairing=(12,40),waist_depth=(0,.18),waist_width=(0,.2),waist_height=(.7,1.7),attachment=(.65,.94),support_inner=(.3,.7),support_outer=(1.05,1.5),anchor_inner=(2.2,3.2),anchor_middle=(3.7,4.6),swing=(.4,.9),clearance=(45,75),curl=(.04,.14),curl_response=(.25,.9))
step=dict(body=.25,end=.4,bulb=.2,shaft=.2,depth=.15,extension=.3,midsection=.3,socket=.12,collar=.15,fairing=12,waist_depth=.06,waist_width=.07,waist_height=.35,attachment=.08,support_inner=.15,support_outer=.15,anchor_inner=.3,anchor_middle=.35,swing=.15,clearance=10,curl=.04,curl_response=.25)
(root/'search-space.json').write_text(json.dumps(dict(baseline=base,bounds=bounds,initialSteps=step,method='Two coordinate rounds, coupled randomized refinement, final coordinate refinement; fully regenerated mesh, weights, bind matrices and tracks; mean alpha-mask IoU on all 256 mapped frames; improving candidates require 256 quarter-frame geometry checks.'),indent=2))
def replace(code,old,new):
 assert old in code,old
 return code.replace(old,new)
def sources(p):
 s=S
 for old,new in [('BODY_HALF_EXTENSION = 1.4',f"BODY_HALF_EXTENSION = {p['extension']}"),('BODY_MIDSECTION = 0.8',f"BODY_MIDSECTION = {p['midsection']}"),('radius, end, bulb = 2.8, 5.4, 2.0',f"radius, end, bulb = {p['body']}, {p['end']}, {p['bulb']}"),('** 0.5',f"** {p['socket']}"),('max(1.5,',f"max({p['shaft']},"),('s / 0.3',f"s / {p['collar']}"),('surface.triangles, 24',f"surface.triangles, {int(p['fairing'])}"),('positions[:, 0] *= 0.85',f"positions[:, 0] *= {p['depth']}"),('z / 1.1',f"z / {p['waist_height']}"),('1 - 0.08 * waist',f"1 - {p['waist_depth']} * waist"),('1 - 0.10 * waist',f"1 - {p['waist_width']} * waist"),('(2.7, 4.4, end)',f"({p['anchor_inner']}, {p['anchor_middle']}, end)"),('math.cos(1.3)',f"math.cos({p['support_outer']})"),('math.cos(0.5)',f"math.cos({p['support_inner']})"),('weights = 0.78',f"weights = {p['attachment']}")]:s=replace(s,old,new)
 a=A
 for old,new in [('0.08 * math.tanh(lean / 0.5)',f"{p['curl']} * math.tanh(lean / {p['curl_response']})"),('swing.angle > 0.6',f"swing.angle > {p['swing']}"),('Quaternion(swing.axis, 0.6)',f"Quaternion(swing.axis, {p['swing']})"),('math.radians(60)',f"math.radians({p['clearance']})")]:a=replace(a,old,new)
 return s,a

def mask(path):
 data=Path(path).read_bytes();off=struct.unpack_from('<I',data,10)[0];w,h=struct.unpack_from('<ii',data,18);bits=struct.unpack_from('<H',data,28)[0];assert bits==32,(path,bits)
 pixels=np.frombuffer(data,np.uint8,abs(w*h)*4,off).reshape(abs(h),w,4)
 if h>0:pixels=pixels[::-1]
 return (pixels[:,:,3]>0).reshape(16,128,16,128).transpose(0,2,1,3).reshape(256,-1)
reference=mask('artifacts/rig-refinement/captures/baked-1.bmp')
env=dict(os.environ,SDL_VIDEODRIVER='x11',DISPLAY=':0',GLOB2_USER_DATA_DIR=str(ROOT/'artifacts/unit-rigs/profile'))
results=json.loads((root/'results.json').read_text()) if (root/'results.json').exists() else []; seen={json.dumps(r['parameters'],sort_keys=True):r for r in results};best=max((r for r in results if r.get('valid')),key=lambda r:r['mean'],default=None)
def validate(folder):
 data=bytearray((folder/'worker-walk.gsr').read_bytes());_,nv,ni,nb,*_=struct.unpack_from('<4s6I',data);off=28+nv*64+ni*4+nb*68+128
 for f in range(256):struct.pack_into('<2f',data,off+f*8,0,f/128)
 (root/'dense.gsr').write_bytes(data)
 subprocess.run([str(ROOT/'artifacts/rig-refinement/evaluate'),str(root/'dense.gsr'),str(root/'dense.bin')],check=True,stdout=subprocess.DEVNULL)
 tri=np.frombuffer(data,'<u4',ni,28+nv*64).reshape(-1,3);rest=np.array([struct.unpack_from('<3f',data,28+i*64) for i in range(nv)]);_,first,inv=np.unique(rest,axis=0,return_index=True,return_inverse=True);faces=inv[tri];sets=[set(f) for f in faces];poses=np.fromfile(root/'dense.bin','<f4').reshape(256,nv,6)
 bad=[];back=0
 for f,pose in enumerate(poses):
  p=pose[:,:3];normal=np.cross(p[tri[:,1]]-p[tri[:,0]],p[tri[:,2]]-p[tri[:,0]]);back=max(back,int(((normal*pose[tri,3:].mean(1)).sum(1)<0).sum()))
  tree=BVHTree.FromPolygons(p[first].tolist(),faces.tolist(),all_triangles=True)
  pairs=[(a,b) for a,b in tree.overlap(tree) if a<b and not sets[a]&sets[b]]
  if pairs:bad.append(dict(sample=f,pairs=len(pairs)))
  if back or bad:break
 result=dict(samplesChecked=f+1,intersections=bad,maxBackwardFaces=back)
 (folder/'validation.json').write_text(json.dumps(result,indent=2))
 return not(back or bad),result

def trial(p,label):
 global best
 key=json.dumps(p,sort_keys=True)
 if key in seen:return seen[key]
 folder=root/f'trial-{len(results):03d}';folder.mkdir(exist_ok=True);start=time.time();r=dict(id=folder.name,label=label,parameters=p)
 try:
  s,a=sources(p);mod=types.ModuleType('worker_surface');mod.__file__=str(ROOT/'tools/skins/worker_surface.py');exec(compile(s,mod.__file__,'exec'),mod.__dict__);sys.modules['worker_surface']=mod
  ns={'__file__':str(ROOT/'tools/skins/author_worker_rig.py'),'__name__':'trial_author'};exec(compile(a,ns['__file__'],'exec'),ns);ns['author'](folder)
  if label=='baseline':assert (folder/'worker-walk.gsr').read_bytes()==(root/'baseline/worker-walk.gsr').read_bytes(),'Baseline rebuild mismatch'
  with (folder/'render.log').open('w') as log:subprocess.run([str(ROOT/'build/linux/client/release/src/skin-preview'),str(folder/'worker-walk.gsr'),str(folder/'gpu'),'--rig-review'],env=env,check=True,stdout=log,stderr=log)
  b=mask(folder/'gpu-1.bmp');iou=(reference&b).sum(1)/(reference|b).sum(1);r.update(mean=float(iou.mean()),minimum=float(iou.min()),p5=float(np.quantile(iou,.05)),coverage=float(b.sum()/reference.sum()),perFrame=iou.tolist())
  if best is None or r['mean']>best['mean']:
   r['valid'],r['validation']=validate(folder)
   if r['valid']:
    best=r;(root/'best.json').write_text(json.dumps(r,indent=2));print('NEW BEST',folder.name,round(r['mean']*100,4),label,flush=True)
 except Exception as e:r['error']=str(e);traceback.print_exc()
 r['seconds']=time.time()-start;results.append(r);seen[key]=r;(folder/'parameters.json').write_text(json.dumps(p,indent=2));(root/'results.json').write_text(json.dumps(results,indent=2))
 # Keep authored assets/parameters for reproducibility, not gigabytes of BMP sheets.
 for style in range(6):
  f=folder/f'gpu-{style}.bmp'
  if f.exists():f.unlink()
 print('TRIAL',len(results),label,round(r.get('mean',0)*100,4),'valid',r.get('valid','not competitive'),'seconds',round(r['seconds'],1),flush=True)
 return r

if __name__=='__main__':
 trial(base,'baseline')
 for round_index,scale in enumerate([1.,.5]):
  for name in base:
   center=best['parameters'].copy()
   for sign in [-1,1]:
    p=center.copy();p[name]=float(np.clip(center[name]+sign*step[name]*scale,*bounds[name]));p[name]=int(round(p[name])) if name=='fairing' else round(p[name],5)
    if p['anchor_middle']>=p['end']-.15:continue
    trial(p,f'coordinate-{round_index+1}-{name}-{sign}')
 # Coupled changes explore interactions that one-at-a-time measurements miss.
 rng=np.random.default_rng(821);names=list(base)
 for i in range(32):
  p=best['parameters'].copy()
  for name in rng.choice(names,size=int(rng.integers(2,6)),replace=False):
   p[name]=float(np.clip(p[name]+rng.uniform(-.75,.75)*step[name],*bounds[name]));p[name]=int(round(p[name])) if name=='fairing' else round(p[name],5)
  trial(p,f'coupled-{i}')
 for name in base:
  center=best['parameters'].copy()
  for sign in [-1,1]:
   p=center.copy();p[name]=float(np.clip(center[name]+sign*step[name]*.25,*bounds[name]));p[name]=int(round(p[name])) if name=='fairing' else round(p[name],5)
   trial(p,f'fine-{name}-{sign}')
 print('COMPLETE',best['id'],best['mean'],flush=True)
