#!/usr/bin/env python3
"""Reproducible per-image finishing studies; never writes game assets."""
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

import super_resolution as sr
from compare_approaches import shadow_aware

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / 'buildings'
OUT = ROOT / 'finishing'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def finish_layer(name, recipe):
    im = Image.open(BUILD / 'corrected' / name).convert('RGBA')
    structural = recipe.get('structural_overlay') and name.startswith('defencetower1b') and name.endswith('r.png')
    if structural:
        raw=Image.open(OUT/'alpha-raw'/name).convert('L').crop((64,64,64+im.width,64+im.height))
        a=np.asarray(raw).astype(float)
        # These layers are translucent crystal geometry, not soft glow markings.
        # Keep the original maximum opacity instead of normalizing to opaque neon.
        a *= np.asarray(im.getchannel('A')).max()/max(1,a.max())
        a=gaussian_filter(a,.45);a[a<3]=0
        im.putalpha(sr.image(a))
        return im
    if recipe.get('soft_shadow') and name.endswith('r.png') and (BUILD/'corrected'/(name[:-5]+'.png')).exists():
        # Team overlays encode translucent glow as well as paint. Restoring their
        # alpha independently made them opaque neon patches in the first trial.
        return im
    if recipe.get('model') == 'anime':
        raw = Image.open(OUT / 'anime-raw' / name).convert('RGB').crop((64,64,64+im.width,64+im.height))
        # Preserve broad source lighting while retaining the alternate model's line work.
        old = np.asarray(im)[:, :, :3].astype(float)
        delta = np.asarray(raw).astype(float) - old
        restored = sr.image(old + recipe.get('model_mix',1) * (delta - recipe.get('color_lock', 0.0) * gaussian_filter(delta,(8,8,0))))
        restored.putalpha(im.getchannel('A'))
        im = restored
    if recipe.get('neural_alpha'):
        raw = Image.open(OUT / 'alpha-raw' / name).convert('L').crop((64,64,64+im.width,64+im.height))
        original = np.asarray(im.getchannel('A'))
        alpha = np.asarray(raw).copy()
        alpha[alpha < 5] = 0
        native = np.asarray(Image.open(sr.ROOT / 'data/gfx' / name).convert('RGBA'))
        shadows = (native[:,:,:3].max(2)<20) & (native[:,:,3]<230)
        shadow = Image.fromarray(np.where(shadows,native[:,:,3],0).astype(np.uint8)).resize(im.size,Image.Resampling.BILINEAR)
        alpha = np.maximum(alpha,np.asarray(shadow))
        if recipe.get('soft_shadow'):
            shadow_region = np.asarray(shadow)>0
            alpha[shadow_region] = original[shadow_region]
            alpha = np.rint(gaussian_filter(alpha.astype(float),.45)).astype(np.uint8)
        alpha = np.rint(original.astype(float) + recipe.get('alpha_mix',1)*(alpha.astype(float)-original)).astype(np.uint8)
        # Connection tiles must retain the original border pixels.
        if name.startswith('wall'):
            alpha[:8]=original[:8];alpha[-8:]=original[-8:]
            alpha[:,:8]=original[:,:8];alpha[:,-8:]=original[:,-8:]
        im.putalpha(Image.fromarray(alpha))
    a = np.asarray(im).copy()
    # Fill transparent RGB before filtering so invisible matte colors cannot bleed in.
    rgb = np.asarray(sr.fill_rgb(im)).astype(float)
    detail = rgb - gaussian_filter(rgb, (1.1, 1.1, 0), mode='nearest')
    # Cap the change at six channel values, and leave translucent edges/shadows alone.
    weight = np.clip((a[:, :, 3].astype(float) - 192) / 63, 0, 1)
    a[:, :, :3] = np.clip(np.rint(a[:, :, :3] + np.clip(
        detail * recipe['detail'], -6, 6) * weight[:, :, None]), 0, 255)
    if recipe['edge_mix']:
        repaired = np.asarray(shadow_aware(name, im.getchannel('A'))).astype(float)
        alpha = a[:, :, 3].astype(float)
        # A partial blend retains fine fragments that a full contour replacement erased.
        a[:, :, 3] = np.rint(alpha + recipe['edge_mix'] * (repaired - alpha))
        # Use filled RGB where the outline extends into originally empty pixels.
        newly_visible = (np.asarray(im)[:, :, 3] == 0) & (a[:, :, 3] > 0)
        a[newly_visible, :3] = rgb[newly_visible]
    return Image.fromarray(a)


def main():
    config = json.loads((ROOT / 'finishing.json').read_text())
    frames = json.loads((BUILD / 'gallery-data.json').read_text())
    recipes = config['recipes']
    assert config['version'] == 1 and config['default'] in recipes
    assert set(config['images']) <= {f['id'] for f in frames}
    for recipe in recipes.values():
        assert 0 <= recipe['edge_mix'] <= 1 and 0 <= recipe['detail'] <= 1
    for trials in config['family_trials'].values():
        assert set(trials) <= recipes.keys()
    inputs = {p: digest(p) for p in (BUILD / 'corrected').glob('*.png')}
    inputs.update({p: digest(p) for p in (BUILD / 'composites').glob('*-corrected.png')})
    (OUT / 'composites').mkdir(parents=True, exist_ok=True)
    rows = []
    for f in frames:
        name = f['id']
        decision = config['images'].get(name, {})
        selected = decision.get('selected', config['default'])
        trials = config['family_trials'].get(f['family'], [])
        if decision.get('locked'):
            assert selected == 'current'
            trials = []
        methods = list(dict.fromkeys(['current', *trials, selected]))
        refpath = BUILD / 'composites' / f'{name}-corrected.png'
        ref = Image.open(refpath).convert('RGBA')
        variants = []
        for method in methods:
            target = OUT / 'composites' / f'{name}-{method}.png'
            if method == 'current':
                shutil.copyfile(refpath, target)
                label = recipes[method]['label']
            elif method.startswith('generator_'):
                assert method not in decision.get('rejected', [])
                shutil.copyfile(ROOT / decision['artifact'], target)
                label = decision.get('artifact_label', 'Generator v1 · preferred visual study')
            else:
                assert method in recipes
                layer_dir = OUT / 'layers' / method
                layer_dir.mkdir(parents=True, exist_ok=True)
                composite = Image.new('RGBA', ref.size)
                for layer in sorted(f['layers'], key=lambda n: n.endswith('r.png')):
                    result = finish_layer(layer, recipes[method])
                    result.save(layer_dir / layer)
                    composite.alpha_composite(result)
                composite.save(target)
                label = recipes[method]['label']
            result = Image.open(target)
            assert result.mode == 'RGBA' and result.size == ref.size
            if method == 'current':
                assert digest(target) == digest(refpath)
            if method == 'detail':
                assert np.array_equal(result.getchannel('A'), ref.getchannel('A'))
            variants.append(dict(id=method, label=label,
                                 path=str(target.relative_to(ROOT)), sha256=digest(target)))
        rows.append(dict(**f, selected=selected, status=decision.get('status', 'unreviewed'),
                         note=decision.get('note', 'Existing upscale retained pending review.'),
                         locked=decision.get('locked', False), variants=variants))
    assert all(digest(p) == h for p, h in inputs.items())
    finalize(rows, recipes)


def finalize(rows, recipes):
    (OUT / 'manifest.json').write_text(json.dumps(rows, indent=2) + '\n')
    for recipe_id, recipe in recipes.items():
        if recipe.get('soft_shadow'):
            for p in (OUT/'layers'/recipe_id).glob('*r.png'):
                if (BUILD/'corrected'/(p.name[:-5]+'.png')).exists():
                    if recipe.get('structural_overlay') and p.name.startswith('defencetower1b'):
                        assert np.asarray(Image.open(p).getchannel('A')).max() <= np.asarray(Image.open(BUILD/'corrected'/p.name).getchannel('A')).max()
                    else:
                        assert np.array_equal(Image.open(p),Image.open(BUILD/'corrected'/p.name))
    for row in rows:
        for variant in row['variants']:
            p=ROOT/variant['path'];im=Image.open(p)
            assert im.mode=='RGBA' and im.size==(row['width']*4,row['height']*4)
            assert digest(p)==variant['sha256']
            if variant['id']=='current':assert digest(p)==digest(BUILD/'composites'/f'{row["id"]}-corrected.png')
    report = dict(frames=len(rows), comparisons=sum(len(f['variants']) for f in rows),
                  trial_frames=sum(len(f['variants']) > 1 for f in rows),
                  source_hashes_unchanged=True, current_copies_byte_identical=True,
                  detail_recipe_alpha_unchanged=True, all_outputs_rgba_exact_size=True,
                  refined_glow_overlays_pixel_identical=True,
                  crystal_opacity_within_original_maximum=True,
                  config_sha256=digest(ROOT / 'finishing.json'))
    (OUT / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    write_viewer(rows, recipes)
    print(json.dumps(report, indent=2))


def write_viewer(rows, recipes):
    template = r'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Glob2 · Finishing workshop</title>
<style>:root{color-scheme:dark;--bg:#304735}*{box-sizing:border-box}body{margin:0;background:#131b16;color:#eaf3ec;font:16px/1.5 system-ui}header,main{padding:24px}h1{margin:0}p{max-width:1000px;color:#bad0c0}a{color:#b5e6c3}nav{position:sticky;top:0;z-index:2;background:#24372a;padding:14px 24px;display:flex;gap:16px;flex-wrap:wrap}select,button{font:inherit;padding:7px;background:#304b38;color:white;border:1px solid #6b8973;border-radius:5px}label{display:inline-flex;gap:8px;align-items:center}article{margin-bottom:36px;border:1px solid #45624d;border-radius:8px;overflow:hidden}article h2,article p{margin:12px 18px}.row{display:flex;overflow:auto}.pane{flex:1;min-width:50%;background:var(--bg)}.caption{background:#263b2d;padding:10px 18px;min-height:48px}.stage{display:flex;justify-content:center;padding:20px}img{max-width:none}.badge{font-size:13px;color:#b3dec0}#count{margin:10px 0}.hint{font-size:14px}button{cursor:pointer}@media(max-width:600px){header,main{padding:12px}}</style>
<header><h1>Finishing workshop</h1><p>Shared upscale, optional finishing recipes, and explicit per-image choices. Compare a recipe across every state of a building. Saved choices show the reviewed experimental set. The two swarm generator images are visual studies with estimated transparency and flattened team layers.</p><a href="approaches.html">Earlier experiments</a> · <a href="FINISHING-REVIEW.md">Recipes and review notes</a> · <a href="finishing.json">Saved choices</a></header>
<nav><label>Family <select id="family"></select></label><label>Right side <select id="method"><option value="selected">Saved choice</option><option value="detail">Mild detail · original outline</option><option value="edges">Gentle edge repair</option><option value="balanced" selected>Gentle edges + mild detail</option></select></label><label>Zoom <select id="zoom"><option value="2">2×</option><option value="4" selected>4×</option><option value="8">8×</option></select></label><label>Background <select id="background"><option value="#304735">Green</option><option value="#111">Black</option><option value="#eee">White</option><option value="checker">Checkerboard</option></select></label></nav><main><div id="count"></div><div id="frames"></div></main>
<script>const data=@@DATA@@,recipes=@@RECIPES@@,$=id=>document.getElementById(id);const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
$('method').innerHTML='<option value="selected">Saved choice</option>'+Object.entries(recipes).filter(([k])=>k!=='current').map(([k,v])=>`<option value="${k}">${esc(v.label)}</option>`).join('');
$('family').innerHTML='<option value="all">All families</option>'+[...new Set(data.map(f=>f.family))].sort().map(f=>`<option>${esc(f)}</option>`).join('');$('family').value='Inn';
function render(){const frames=data.filter(f=>$('family').value==='all'||f.family===$('family').value||f.uses.some(u=>u.family===$('family').value)),z=+$('zoom').value,m=$('method').value;$('count').textContent=`${frames.length} states / frames · left: existing upscale · right: selected comparison`;$('frames').innerHTML=frames.map(f=>{const requested=m==='selected'?f.selected:m,v=f.variants.find(v=>v.id===requested)||f.variants.find(v=>v.id===f.selected),base=f.variants.find(v=>v.id==='current');return `<article><h2>${esc(f.id)} <span class="badge">${esc(f.label)}</span></h2><p>${esc(f.status)}${f.locked?' · locked reference':''} — ${esc(f.note)}</p>${v.id!==requested?'<p class="hint">This recipe has not been tested here; showing the saved choice.</p>':''}<div class="row">${[base,v].map(v=>`<div class="pane" style="min-width:max(50%,${f.width*z+40}px)"><div class="caption">${esc(v.label)}</div><div class="stage"><img loading="lazy" src="${v.path}" alt="${esc(f.id+' — '+v.label)}" width="${f.width*z}" height="${f.height*z}"></div></div>`).join('')}</div></article>`}).join('')}
['family','method','zoom'].forEach(id=>$(id).onchange=render);$('background').onchange=()=>{const b=$('background').value;document.documentElement.style.setProperty('--bg',b==='checker'?'repeating-conic-gradient(#65736a 0% 25%,#3e4e44 0% 50%) 0 / 24px 24px':b)};render();</script></html>'''
    (ROOT / 'finishing.html').write_text(template.replace('@@DATA@@', json.dumps(rows).replace('<', '\\u003c')).replace('@@RECIPES@@',json.dumps(recipes)))


if __name__ == '__main__':
    main()
