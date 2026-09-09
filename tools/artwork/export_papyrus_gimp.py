"""Run with GIMP 2.10 python-fu-eval; preserve source layer opacity and modes."""
from gimpfu import *
import os, json, hashlib
root=os.environ.get('GLOB2_ART_ROOT',os.getcwd())
out=os.path.join(root,'datasrc/gfx/derived/papyrus-native')
if not os.path.isdir(out): os.makedirs(out)
records=[]
for stage in range(5):
    name='ressource%d' % (20+stage)
    relative='datasrc/gfx/originals/terrain/papyrus.xcf'
    path=os.path.join(root,relative)
    image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
    layers=[dict(index=i,name=l.name,visible=l.visible,opacity=l.opacity,mode=int(l.mode),offsets=l.offsets) for i,l in enumerate(image.layers)]
    for i,layer in enumerate(image.layers): layer.visible=(i==stage)
    pad=gimp.Layer(image,'export transparent backing',image.width,image.height,RGBA_IMAGE,100,NORMAL_MODE)
    image.add_layer(pad,len(image.layers));pdb.gimp_drawable_fill(pad,TRANSPARENT_FILL)
    size=[image.width,image.height]
    merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
    export=os.path.join(out,name+'.png')
    pdb.file_png_save_defaults(image,merged,export,export,run_mode=RUN_NONINTERACTIVE)
    pdb.gimp_image_delete(image)
    records.append(dict(source=relative,source_sha256=hashlib.sha256(open(path,'rb').read()).hexdigest(),file=name+'.png',sha256=hashlib.sha256(open(export,'rb').read()).hexdigest(),native_size=size,layers=layers,selected_layer=stage))
with open(os.path.join(out,'manifest.json'),'w') as f:
    json.dump(dict(version=1,images=records),f,indent=2,separators=(',', ': '))
    f.write('\n')
