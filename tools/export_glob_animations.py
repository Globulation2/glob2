"""Bake original Blender metaball animations for the playable 3D trial.
Run with Blender 3.6: blender -b --disable-autoexec --python tools/export_glob_animations.py -- ROOT
The original .blend files remain unchanged. Each frame is a triangle mesh because
metaball animation changes topology; a conventional skeletal glTF export loses it.
"""
import bpy, sys, struct, json
from pathlib import Path
from mathutils import Vector
root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
out = root / 'data/models3d'; out.mkdir(parents=True, exist_ok=True)
clips = {'worker-walk':'glob-worker-walk.blend','worker-swim':'glob-worker-swim.blend',
         'worker-harvest':'glob-worker-harvest.blend','warrior-walk':'glob-warrior-walk.blend',
         'warrior-swim':'glob-warrior-swim.blend','warrior-fight':'glob-warrior-fight.blend','explorer':'explorer.blend'}
manifest = {}
for name, source in clips.items():
 bpy.ops.wm.open_mainfile(filepath=str(root/'datasrc/gfx/globules'/source),use_scripts=False)
 scene=bpy.context.scene
 # RotEmpty turns the model through the eight sprite directions. The game now
 # supplies heading, so remove only that turntable animation, preserving the rig.
 for o in scene.objects:
  if o.name.startswith('RotEmpty'):
   o.animation_data_clear(); o.rotation_euler=(0,0,0)
 for mb in bpy.data.metaballs: mb.resolution=max(mb.resolution,0.45)
 actions=[o.animation_data.action for o in scene.objects if o.type=='ARMATURE' and o.animation_data and o.animation_data.action]
 action=actions[0] if actions else None
 start,end=tuple(action.frame_range) if action else (1,16)
 frames=[]
 for i in range(16):
  t=start+(end-start)*i/16
  scene.frame_set(int(t),subframe=t-int(t)); graph=bpy.context.evaluated_depsgraph_get()
  vertices=[]
  for o in scene.objects:
   if o.type not in ('META','MESH','SURFACE','CURVE') or o.name.lower().startswith(('plane','floor','ground')): continue
   eo=o.evaluated_get(graph); mesh=eo.to_mesh()
   if mesh is None: continue
   mesh.calc_loop_triangles(); matrix=eo.matrix_world; normal=matrix.to_3x3().inverted().transposed()
   for tri in mesh.loop_triangles:
    mat=mesh.materials[tri.material_index] if len(mesh.materials)>tri.material_index else None
    color=tuple(mat.diffuse_color[:3]) if mat else (0.1,0.8,0.3)
    tint=1.0 if max(color)-min(color)>0.2 else 0.0
    for vi in tri.vertices:
     v=mesh.vertices[vi]; p=matrix@v.co; n=(normal@v.normal).normalized()
     vertices.append([*p,*n,*color,tint])
   eo.to_mesh_clear()
  if not vertices: raise RuntimeError('No mesh in '+source)
  frames.append(vertices)
 lo=[min(v[a] for f in frames for v in f) for a in range(3)]
 hi=[max(v[a] for f in frames for v in f) for a in range(3)]
 scale=25/max(hi[0]-lo[0],hi[1]-lo[1])
 with (out/(name+'.g3d')).open('wb') as f:
  f.write(b'G3D1');f.write(struct.pack('<I',len(frames)))
  for frame in frames:
   f.write(struct.pack('<I',len(frame)))
   for v in frame:
    v[0]=(v[0]-(lo[0]+hi[0])/2)*scale;v[1]=(v[1]-(lo[1]+hi[1])/2)*scale;v[2]=(v[2]-lo[2])*scale
    f.write(struct.pack('<10f',*v))
 manifest[name]={'source':source,'frames':len(frames),'range':[start,end],'triangles':[len(f)//3 for f in frames],'bounds':[lo,hi]}
 print('EXPORTED',name,manifest[name],flush=True)
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
