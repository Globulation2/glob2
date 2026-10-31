#!/usr/bin/env python3
"""Capture the actual registered Thai catalogs through the production native preview harness."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read_native(path):
    lines=path.read_text(encoding='utf-8').splitlines()
    if len(lines)%2:raise ValueError('Native catalog has an unpaired key/value line')
    result=dict(zip(lines[::2],lines[1::2]))
    if len(result)!=len(lines)//2:raise ValueError('Duplicate native catalog key would hide coverage')
    if any(not key.startswith('[') or not key.endswith(']') for key in result):raise ValueError('Invalid native catalog key')
    return result


def execute_capture(command, root, env, log):
    # Pass native paths as argument elements: Windows/MSYS spaces must not invoke a shell.
    with log.open('w',encoding='utf-8') as stream:
        subprocess.run(command,cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=180)


def capture_environment(profile, width):
    env=dict(os.environ,GLOB2_USER_DATA_DIR=str(profile),SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software',SDL_AUDIODRIVER='dummy')
    env.pop('GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS',None)
    env.pop('GLOB2_MOBILE_UI',None)
    if width==390:env['GLOB2_MOBILE_UI']='1'
    return env


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--locale',choices=['th'],default='th');args=p.parse_args()
    root=Path(__file__).resolve().parents[1];binary=args.binary.resolve();output=args.output.resolve()
    if not output.is_relative_to(root/'artifacts') or output.exists():raise ValueError('Use a fresh capture directory under checkout artifacts')
    native=read_native(root/'data/texts.th.txt')
    if set(native)!=set(read_native(root/'data/texts.en.txt')):raise ValueError('Native Thai key coverage differs from English')
    online=json.loads((root/'platform/packages/i18n/locales/th.json').read_text(encoding='utf-8'))
    online_source=json.loads((root/'platform/packages/i18n/locales/en.json').read_text(encoding='utf-8'))
    if set(online)!=set(online_source):raise ValueError('Online Thai key coverage differs from English')
    output.mkdir(parents=True);inputs=output/'inputs';inputs.mkdir();full=inputs/'full';matrix=inputs/'matrix'
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True,encoding='utf-8').strip()
    source_paths=['data/texts.en.txt','data/texts.th.txt','data/texts.keys.txt',
                  'platform/packages/i18n/locales/en.json','platform/packages/i18n/locales/th.json',
                  'data/fonts/scripts/NotoSansThai-Regular.ttf','libgag/src/TrueTypeFont.cpp',
                  'libgag/include/TextBreaks.h','libgag/src/ui/TextLayout.cpp',
                  'tools/TranslationPreviewHarness.cpp']
    committed_hashes={name:hashlib.sha256(subprocess.check_output(['git','show',revision+':'+name],cwd=root)).hexdigest() for name in source_paths}
    checkout_hashes={name:sha(root/name) for name in source_paths}
    metadata={'locale':'th','revision':revision,'committed_source_file_sha256':committed_hashes,'checkout_source_file_sha256':checkout_hashes,'kind':'Exact registered catalogs, not a draft/pilot translation claim',
              'native_catalog_sha256':sha(root/'data/texts.th.txt'),'online_catalog_sha256':sha(root/'platform/packages/i18n/locales/th.json')}
    samples=['[language]','[language incomplete]','[guard area]','[settings]','[inn]','[Confirm %0]',
             '[swimmingpool explanation]','[lesson swimming briefing]','[settings Display name]',
             '[lesson defensive-tactics guard hint]','[lesson healing briefing]','[lesson upgrades briefing]',
             '[the swimmingpool is under attack]','[Portage Lakes needs separate swimming lakes on this seed.]',
             '[Worker Ratio]','[Worker]','[workers]','[Starting workers]',
             '[any explorer]','[Explorer Ratio]','[Explorer]','[explorers]',
             '[Warrior Ratio]','[Warrior]','[warriors]','[No warriors]',
             '[inn explanation]','[swarm explanation]','[swarm]','[Width]','[Height]','[River width]',
             '[Sound or silence?]','[Sound]','[settings Resolution]','[settings High-resolution artwork]',
             '[settings High resolution]','[settings Missing font]','[play online]','[quit]']
    web_samples=['Saving your project…','Generation is unavailable.','Wait for the current request to finish.',
                 'Generation is unavailable. Saved projects remain accessible.',
                 'Generation is unavailable. Saved music remains accessible.',
                 'An available Map credit is needed to chat or build.','An available Music credit is needed to chat or build.',
                 'New soundtrack','Finish active work or resolve the revision conflict first.',
                 'Choose a valid request cap.',' · comparing','The last build did not complete.',
                 'Project title','Current saved draft','New map','Request cap']
    web_samples+=sorted(k for k in online if k.startswith('Discussion spends no credits; 1 available ') and 'Creation requests build automatically' in k)
    if len(samples)!=40 or len(web_samples)!=20:raise ValueError('Update the explicitly bounded representative sample inventory')
    for folder,values,web in [(full,native,online),(matrix,{k:native[k] for k in samples},{k:online[k] for k in web_samples})]:
        folder.mkdir();(folder/'native.json').write_text(json.dumps(values,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (folder/'online.json').write_text(json.dumps(web,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (folder/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    report={'platform':platform.platform(),'machine':platform.machine(),'executable_sha256':sha(binary),
            'revision':revision,'kind':'registered-catalog-input-snapshot','committed_source_file_sha256':committed_hashes,'checkout_source_file_sha256':checkout_hashes,
            'catalogs':metadata,'native_keys':len(native),'online_keys':len(online),'commands':[],'passed':False,
            'scope':'Native-platform production font/shaping/layout via SDL dummy driver and software renderer. Matrix covers10/13/20 pixels at1x/2x. No GPU, physical monitor, mobile OS or complete catalog visual-coverage claim.',
            'visual_review_required':True,'dynamic_libraries':{p.name:sha(p) for p in binary.parent.glob('*.dll')}}
    # The harness reads pilot.parent.parent/manifest.json; preserve the actual
    # committed registered-source revision there before any native capture.
    (output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    jobs=[('matrix',matrix,1280,900,'')]
    for width,height in [(1280,800),(390,844)]:
        jobs += [('screen',full,width,height,name) for name in ['main','settings','lobby','lobby-players']]
        jobs += [('game',full,width,height,name) for name in ['gameplay','tutorial']]
    try:
        for index,(mode,pilot,width,height,name) in enumerate(jobs):
            dest=output/(f'{width}x{height}-'+(name or 'matrix'));dest.mkdir()
            profile=output/'profiles'/str(index);profile.mkdir(parents=True)
            env=capture_environment(profile,width)
            command=[str(binary),mode,str(root),str(pilot),str(dest),str(width),str(height)]+([name] if name else [])
            report['commands'].append(command)
            execute_capture(command,root,env,dest/'capture.log')
            if not list(dest.glob('*.png')):raise ValueError('Successful preview produced no PNG: '+str(dest))
            if mode=='matrix':
                result=json.loads((dest/'font-matrix.json').read_text(encoding='utf-8'))
                if any(not row['has_glyphs'] for row in result['samples']):raise ValueError('Thai sample glyph coverage failed')
                if any(not line['drawn_fits_width'] or not line['drawn_fits_height'] for row in result['samples'] for line in row['lines']):raise ValueError('Thai matrix destination clips text')
        report['screenshots']={str(p.relative_to(output)):sha(p) for p in output.rglob('*.png')}
        report['passed']=True
    finally:(output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
