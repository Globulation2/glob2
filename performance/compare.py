import os, pathlib, subprocess, json, sys
from PIL import Image, ImageChops
root=pathlib.Path.cwd(); out=root/'artifacts/skins/performance'
for run in range(int(sys.argv[1]) if len(sys.argv)>1 else 1,4):
    modes=['optimized','baseline'] if run%2 else ['baseline','optimized']
    for mode in modes:
        profile=out/f'{mode}-{run}-profile'; profile.mkdir(exist_ok=True)
        env=dict(os.environ,GLOB2_USER_DATA_DIR=str(profile),GLOB2_SKIN_PREVIEW_DIR=str(root/'artifacts/skins/units'),SKIN_PREVIEW_SAVE=str(root/'artifacts/skins/initial.game.gz'),SKIN_PREVIEW_CAPTURE='final.bmp',SKIN_PREVIEW_BENCHMARK='crowd',SKIN_BENCH_FRAMES='180',SKIN_BENCH_WARMUP='32',SDL_VIDEODRIVER='x11',LD_LIBRARY_PATH=str(root/'artifacts/skins/sdk/lib'))
        with (out/f'{mode}-{run}-matched.log').open('w') as log:
            subprocess.run(['xvfb-run','-a','/usr/bin/time','-v','-o',str(out/f'{mode}-{run}-matched-resource.log'),str(out/f'skin-game-preview-{mode}'),'-g','-m','-s800x600'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(mode,run,flush=True)
    for name in ['crowd-classic.bmp','crowd-skinned.bmp']:
        a=Image.open(out/f'baseline-{run}-profile'/name).convert('RGB')
        b=Image.open(out/f'optimized-{run}-profile'/name).convert('RGB')
        assert a.size==b.size
        d=ImageChops.difference(a,b)
        changed=sum(pixel!=(0,0,0) for pixel in d.getdata())
        maximum=max(high for low,high in d.getextrema())
        print('Pixel comparison',run,name,'changed',changed,'maxChannelDelta',maximum,flush=True)
        if name=='crowd-classic.bmp': assert changed==0
        assert changed< a.width*a.height/1000 and maximum<=8,(run,name,changed,maximum)
    print('Matched pair passed checksum and bounded pixel comparison',run,flush=True)
