"""Run with GIMP 2.10 python-fu-eval; preserve source layer opacity and modes."""
from gimpfu import *
import os, json, hashlib
root=os.environ.get('GLOB2_ART_ROOT',os.getcwd())
out=os.path.join(root,'datasrc/gfx/derived/tree-native')
if not os.path.isdir(out): os.makedirs(out)
records=[]
for name in ['trees_1_1','trees_1_2','trees_2_1','trees_3_1','trees_4_1']:
    relative='datasrc/gfx/originals/resources/tree/'+name+'.xcf'
    path=os.path.join(root,relative)
    image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
    layers=[dict(index=i,name=l.name,visible=l.visible,opacity=l.opacity,mode=int(l.mode),offsets=l.offsets) for i,l in enumerate(image.layers)]
    size=[image.width,image.height]
    merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
    export=os.path.join(out,name+'.png')
    pdb.file_png_save_defaults(image,merged,export,export,run_mode=RUN_NONINTERACTIVE)
    pdb.gimp_image_delete(image)
    records.append(dict(source=relative,source_sha256=hashlib.sha256(open(path,'rb').read()).hexdigest(),file=name+'.png',sha256=hashlib.sha256(open(export,'rb').read()).hexdigest(),native_size=size,layers=layers))
with open(os.path.join(out,'manifest.json'),'w') as f: json.dump(dict(version=1,images=records),f,indent=2)
