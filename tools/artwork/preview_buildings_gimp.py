from gimpfu import *
import os,json,glob
# Run through GIMP 2.10's python-fu-eval from the repository root.
# Only disposable preview images are saved; originals are never resaved.
root=os.environ.get('GLOB2_ART_ROOT',os.getcwd())
out=root+'/.cache/original-art/previews'
if not os.path.isdir(out):os.makedirs(out)
with open(root+'/datasrc/gfx/provenance/building-map.json') as f:mappings=json.load(f)
records=[]
for mapping in mappings:
 path=os.path.join(root,mapping['source'])
 image=pdb.gimp_file_load(path,path,run_mode=RUN_NONINTERACTIVE)
 name=os.path.splitext(os.path.basename(path))[0]
 rec={'file':os.path.basename(path),'width':image.width,'height':image.height,'layers':[]}
 for index,layer in enumerate(image.layers):
  rec['layers'].append({'index':index,'name':layer.name,'visible':layer.visible,'width':layer.width,'height':layer.height,'offsets':layer.offsets,'opacity':layer.opacity})
  solo=pdb.gimp_image_new(image.width,image.height,RGB)
  copy=pdb.gimp_layer_new_from_drawable(layer,solo)
  pdb.gimp_image_insert_layer(solo,copy,None,0)
  pdb.gimp_item_set_visible(copy,True)
  export=os.path.join(out,name+'-layer-'+str(index)+'.png')
  pdb.file_png_save_defaults(solo,copy,export,export,run_mode=RUN_NONINTERACTIVE)
  pdb.gimp_image_delete(solo)
 for index,layer in enumerate(image.layers):
  pdb.gimp_item_set_visible(layer,index in mapping['preview_layers'])
 merged=pdb.gimp_image_merge_visible_layers(image,CLIP_TO_IMAGE)
 export=os.path.join(out,name+'.png')
 pdb.file_png_save_defaults(image,merged,export,export,run_mode=RUN_NONINTERACTIVE)
 pdb.gimp_image_delete(image)
 records.append(rec)
with open(out+'/layers.json','w') as f:json.dump(records,f,indent=2)
