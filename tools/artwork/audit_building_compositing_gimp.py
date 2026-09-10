"""GIMP 2.10 audit only: compare direct XCF composites with split runtime groups."""
from gimpfu import *
import os,json
root=os.environ.get('GLOB2_ART_ROOT',os.getcwd())
out=os.path.join(root,'.cache/original-art/compositing-audit')
if not os.path.isdir(out):os.makedirs(out)
recipes=[('hosp0b0','datasrc/gfx/originals/buildings/hosp/level-1/hopital1.xcf',[1,2,3,4,5],[0]),
         ('defencetower1b0','datasrc/gfx/originals/buildings/defencetower/level-2/tower2.xcf',[2,3,4,5,6],[0,1]),
         ('tower-lowres','datasrc/gfx/originals/buildings/defencetower/level-2/tower1.xcf',[1,2],[0])]
for frame,source,base,team in recipes:
    for kind,selected,pad in [('direct',base+team,False),('base',base,True),('team',team,True)]:
        path=os.path.join(root,source)
        image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
        for i,layer in enumerate(image.layers):pdb.gimp_item_set_visible(layer,i in selected)
        if pad:
            background=pdb.gimp_layer_new(image,image.width,image.height,RGBA_IMAGE,'transparent export canvas',100,NORMAL_MODE)
            pdb.gimp_image_insert_layer(image,background,None,len(image.layers))
            pdb.gimp_drawable_fill(background,TRANSPARENT_FILL)
        merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
        path=os.path.join(out,frame+'-'+kind+'.png')
        pdb.file_png_save_defaults(image,merged,path,path,run_mode=RUN_NONINTERACTIVE)
        pdb.gimp_image_delete(image)
