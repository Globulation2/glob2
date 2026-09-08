#!/usr/bin/env python3
"""Controlled five-sprite comparison; never replaces accepted output files."""
import json
import hashlib
from pathlib import Path
import cv2
import numpy as np
from scipy.ndimage import gaussian_filter, distance_transform_edt
from PIL import Image, ImageDraw, ImageFont
import super_resolution as sr

ROOT=sr.ROOT
OUT=ROOT/'experiments/ai-upscale/approaches'
BUILD=OUT.parent/'buildings'
METHODS=[
 ('nearest','Original · nearest','Baseline'),
 ('baseline','Ordinary bicubic','Baseline'),
 ('current','Current pipeline','Reference'),
 ('general_raw','General model · no RGB blend','Model / color'),
 ('anime','Anime 6B · same constraints','Alternate model'),
 ('video','AnimeVideo v3 · same constraints','Alternate model'),
 ('smooth','Current RGB · smooth contour','Alpha only'),
 ('polygon','Current RGB · simplified contour','Alpha only'),
 ('raw_polygon','General raw RGB · contour','Combined'),
 ('anime_polygon','Anime 6B · contour','Combined'),
 ('video_polygon','AnimeVideo v3 · contour','Combined'),
 ('adaptive','Current RGB · shadow-aware edges','Revised edge treatment'),
 ('anime_adaptive','Anime 6B · shadow-aware edges','Revised edge treatment'),
 ('general_adaptive','General raw · shadow-aware edges','Revised edge treatment'),
 ('clean_polygon','Current RGB · clean straight edges','Revised edge treatment')]


def reconstruct_alpha(alpha, method, preserve_remote=True):
    a=np.array(alpha).astype(float)/255
    solid=a>=0.5
    if not solid.any():return alpha.copy()
    # Operate at output resolution. The smooth method rounds stair steps;
    # polygon approximation also replaces runs of steps with straight segments.
    smoothed=gaussian_filter(solid.astype(float),2.5)
    if method=='smooth':
        repaired=np.clip((smoothed-.5)/.16+.5,0,1)
    else:
        contours,_=cv2.findContours((smoothed>=.5).astype(np.uint8),cv2.RETR_LIST,cv2.CHAIN_APPROX_SIMPLE)
        simplified=[cv2.approxPolyDP(c,2.5,True)*4 for c in contours]
        high=np.zeros((a.shape[0]*4,a.shape[1]*4),np.uint8)
        cv2.drawContours(high,simplified,-1,255,cv2.FILLED)
        repaired=np.array(Image.fromarray(high).resize(alpha.size,Image.Resampling.LANCZOS))/255
    # Keep remote partial-opacity shadows. Within six output pixels of opaque
    # geometry, let the reconstructed contour replace the bilinear boundary.
    nearby=distance_transform_edt(~solid)<=6
    return sr.image((np.where(nearby,repaired,a) if preserve_remote else repaired)*255)


def crop_raw(directory,name):
    w,h=Image.open(ROOT/'data/gfx'/name).size
    return Image.open(directory/name).convert('RGB').crop((64,64,64+w*4,64+h*4))


def shadow_aware(name,alpha,method='smooth'):
    native=Image.open(ROOT/'data/gfx'/name).convert('RGBA')
    a=np.array(native)
    if a[:,:,3].max()<230:return alpha.copy()
    # Preserve dark translucent cast shadows, which a binary 50% threshold
    # otherwise turns into opaque black shapes. This is a test heuristic.
    shadow=(a[:,:,:3].max(2)<20)&(a[:,:,3]<230)
    geometry=a[:,:,3].copy();geometry[shadow]=0
    geometry=Image.fromarray(geometry).resize(alpha.size,Image.Resampling.BILINEAR)
    repaired=reconstruct_alpha(geometry,method,preserve_remote=False)
    shadow_alpha=np.where(shadow,a[:,:,3],0).astype(np.uint8)
    shadow_alpha=np.array(Image.fromarray(shadow_alpha).resize(alpha.size,Image.Resampling.BILINEAR))
    return Image.fromarray(np.maximum(np.array(repaired),shadow_alpha))


def constrained(raw,base):
    a=np.array(raw).astype(float);b=np.array(base.convert('RGB')).astype(float)
    delta=a-b
    return sr.image(b+.65*(delta-gaussian_filter(delta,(8,8,0),mode='nearest')))


def layer(name,method):
    if method in ('nearest','baseline'):return Image.open(BUILD/method/name).convert('RGBA')
    current=Image.open(BUILD/'corrected'/name).convert('RGBA')
    baseline=Image.open(BUILD/'baseline'/name).convert('RGBA')
    alpha=baseline.getchannel('A')
    if method in ('general_raw','raw_polygon','general_adaptive'):rgb=crop_raw(BUILD/'raw',name)
    elif method.startswith('anime'):rgb=constrained(crop_raw(OUT/'anime',name),baseline)
    elif method.startswith('video'):rgb=constrained(crop_raw(OUT/'video',name),baseline)
    else:rgb=current.convert('RGB')
    if method=='clean_polygon':alpha=shadow_aware(name,alpha,'polygon')
    elif method=='smooth':alpha=reconstruct_alpha(alpha,'smooth')
    elif method=='polygon' or method.endswith('_polygon'):alpha=reconstruct_alpha(alpha,'polygon')
    elif method=='adaptive' or method.endswith('_adaptive'):alpha=shadow_aware(name,alpha)
    rgb.putalpha(alpha)
    return rgb


def extract_matte(path,size):
    raw=Image.open(path).convert('RGB')
    rgb=np.array(raw).astype(float)/255
    # Magenta chroma key, then unmix the matte. Black cast shadows are retained.
    # This is an estimated matte, not a model-provided alpha channel.
    a=np.clip(1-(np.minimum(rgb[:,:,0],rgb[:,:,2])-rgb[:,:,1]),0,1)
    a[a<.05]=0
    unmixed=np.clip((rgb-(1-a[:,:,None])*np.array([1.,0.,1.]))/np.maximum(a[:,:,None],1/255),0,1)
    result=sr.image(np.dstack([unmixed,a])*255)
    return result.resize(size,Image.Resampling.LANCZOS)


def main():
    frames=json.loads((OUT/'frames.json').read_text())
    ids=['inn0c0','hosp0b1','swarm0b0','pool0b0','school1b0']
    frames.sort(key=lambda f:ids.index(f['id']))
    metrics=[]
    for f in frames:
        ref=Image.open(BUILD/'composites'/f'{f["id"]}-corrected.png').convert('RGBA')
        f['size']=list(ref.size);f['control']=f['id'] in ['pool0b0','school1b0']
        f['variants']=[]
        for method,label,category in METHODS:
            result=Image.new('RGBA',ref.size)
            for n in sorted(f['layers'],key=lambda n:n.endswith('r.png')):
                result.alpha_composite(layer(n,method),(0,0))
            result.save(OUT/'variants'/f'{f["id"]}-{method}.png')
            f['variants'].append(dict(id=method,label=label,category=category))
        if f['id']=='swarm0b0':extras=[('llm1','swarm-v1.png','Generator v1 · clean facets'),('llm2','swarm-v2.png','Generator v2 · texture revision')]
        elif f['id']=='inn0c0':extras=[('llm1','inn-v1.png','Generator · consistent edges')]
        else:extras=[]
        for method,file,label in extras:
            extract_matte(OUT/'llm'/file,ref.size).save(OUT/'variants'/f'{f["id"]}-{method}.png')
            f['variants'].append(dict(id=method,label=label,category='Generative repaint / estimated alpha',raw='llm/'+file))
        mask=np.array(ref.getchannel('A'))>=128
        for variant in f['variants']:
            im=Image.open(OUT/'variants'/f'{f["id"]}-{variant["id"]}.png')
            candidate=np.array(im.getchannel('A'))>=128
            metrics.append(dict(frame=f['id'],method=variant['id'],size=list(im.size),
                                alpha_iou=float((mask&candidate).sum()/max(1,(mask|candidate).sum())),
                                alpha_equal=bool(np.array_equal(ref.getchannel('A'),im.getchannel('A')))))
        # Current references must remain byte-identical to the accepted pixels.
        assert np.array_equal(ref,Image.open(OUT/'variants'/f'{f["id"]}-current.png'))
    (OUT/'gallery.json').write_text(json.dumps(frames,indent=2)+'\n')
    (OUT/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
    models=['realesrgan-x4plus','realesrgan-x4plus-anime','realesr-animevideov3-x4']
    provenance={name:{ext:hashlib.sha256((sr.MODELS/(name+'.'+ext)).read_bytes()).hexdigest() for ext in ['bin','param']} for name in models}
    provenance['controls']={f['id']:hashlib.sha256((BUILD/'composites'/f'{f["id"]}-corrected.png').read_bytes()).hexdigest() for f in frames if f['control']}
    (OUT/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    sheets(frames)
    print('Created',sum(len(f['variants']) for f in frames),'comparison images')


def sheets(frames):
    for f in frames:
        w,h=f['size']; cw=max(w,360)+32;ch=h+98
        variants=f['variants'];columns=3
        sheet=Image.new('RGB',(cw*columns,80+ch*((len(variants)+2)//3)),(25,33,28));d=ImageDraw.Draw(sheet)
        try:font=ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf',20)
        except OSError:font=ImageFont.load_default()
        d.text((18,18),f['id']+(' · successful reference' if f['control'] else ' · problem sprite'),font=font,fill='white')
        for i,v in enumerate(variants):
            x=(i%3)*cw;y=80+(i//3)*ch
            d.text((x+12,y+8),v['label'],font=font,fill='white')
            d.text((x+12,y+34),v['category'],font=font,fill='#aec6b3')
            im=Image.open(OUT/'variants'/f'{f["id"]}-{v["id"]}.png')
            sheet.paste(im,(x+16,y+70),im)
        sheet.save(OUT/f'{f["id"]}-methods.png')


if __name__=='__main__':main()
