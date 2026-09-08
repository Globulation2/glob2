"""Export saved visible UI/terrain composites for review; never resave originals."""
from gimpfu import *
import os, json, hashlib
root=os.getcwd()
out=os.path.join(root,'.cache/original-art/ui-audit')
if not os.path.isdir(out): os.makedirs(out)
records=[]
for folder in ['ui/controls','terrain']:
    directory=os.path.join(root,'datasrc/gfx/originals',folder)
    for name in sorted(os.listdir(directory)):
        if not name.endswith('.xcf'): continue
        path=os.path.join(directory,name)
        image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
        layers=[dict(index=i,name=l.name,visible=l.visible,opacity=l.opacity,mode=int(l.mode)) for i,l in enumerate(image.layers)]
        pad=gimp.Layer(image,'export transparent backing',image.width,image.height,RGBA_IMAGE,100,NORMAL_MODE)
        image.add_layer(pad,len(image.layers));pdb.gimp_drawable_fill(pad,TRANSPARENT_FILL)
        merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
        export=os.path.join(out,name+'.png')
        pdb.file_png_save_defaults(image,merged,export,export,run_mode=RUN_NONINTERACTIVE)
        records.append(dict(source=os.path.relpath(path,root),source_sha256=hashlib.sha256(open(path,'rb').read()).hexdigest(),size=[image.width,image.height],file=name+'.png',layers=layers))
        pdb.gimp_image_delete(image)
with open(os.path.join(out,'manifest.json'),'w') as f:
    json.dump(records,f,indent=2,separators=(',', ': '));f.write('\n')
