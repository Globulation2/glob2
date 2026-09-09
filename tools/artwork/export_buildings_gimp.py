"""GIMP 2.10: export verified base/team groups, baking saved opacity and modes."""
from gimpfu import *
import os,json,hashlib
root=os.environ.get('GLOB2_ART_ROOT',os.getcwd())
out=os.path.join(root,'datasrc/gfx/derived/building-native')
if not os.path.isdir(out):os.makedirs(out)
config=os.path.join(root,'datasrc/gfx/provenance/building-runtime-recipes.json')
recipes=json.load(open(config))
records=[]
for recipe in recipes['frames']:
    path=os.path.join(root,recipe['source'])
    exports=[]
    for role in ['base','team']:
        image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
        size=[image.width,image.height]
        layers=[dict(index=i,name=l.name,opacity=l.opacity,mode=int(l.mode),offsets=l.offsets) for i,l in enumerate(image.layers)]
        for i,layer in enumerate(image.layers):pdb.gimp_item_set_visible(layer,i in recipe['layers'][role])
        # PNG export of a lone layer ignores its layer opacity. A transparent
        # bottom layer forces a real composite even for single-layer team groups.
        background=pdb.gimp_layer_new(image,image.width,image.height,RGBA_IMAGE,'transparent export canvas',100,NORMAL_MODE)
        pdb.gimp_image_insert_layer(image,background,None,len(image.layers))
        pdb.gimp_drawable_fill(background,TRANSPARENT_FILL)
        merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
        filename=recipe['id']+('r' if role=='team' else '')+'.png'
        dest=os.path.join(out,filename)
        pdb.file_png_save_defaults(image,merged,dest,dest,run_mode=RUN_NONINTERACTIVE)
        pdb.gimp_image_delete(image)
        exports.append(dict(file=filename,role=role,sha256=hashlib.sha256(open(dest,'rb').read()).hexdigest(),selected_layers=recipe['layers'][role]))
    records.append(dict(id=recipe['id'],source=recipe['source'],source_sha256=hashlib.sha256(open(path,'rb').read()).hexdigest(),native_size=size,source_layers=layers,exports=exports))
with open(os.path.join(out,'manifest.json'),'w') as f:
    json.dump(dict(version=1,recipe_sha256=hashlib.sha256(open(config,'rb').read()).hexdigest(),frames=records),f,indent=2,separators=(',', ': '))
    f.write('\n')
