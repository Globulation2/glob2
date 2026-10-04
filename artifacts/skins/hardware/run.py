from pathlib import Path
import subprocess, os, re, json, statistics, hashlib, time
out=Path('artifacts/skins/hardware')
env=os.environ | {'SDL_VIDEO_DRIVER':'x11','DISPLAY':':0','__GL_SYNC_TO_VBLANK':'0'}
binary='build/linux/client/release/src/skin-preview'
(out/'glxinfo.log').write_text(subprocess.check_output(['glxinfo','-B'],env=env,text=True))
(out/'gpu-info.log').write_text(subprocess.check_output(['nvidia-smi'],text=True))
records=[]
for mode in ['--benchmark','--benchmark-pages']:
    for trial in range(10):
        for revision in (['old','new'] if trial%2==0 else ['new','old']):
            assets='artifacts/skins/benchmark-old' if revision=='old' else 'artifacts/skins/native/white'
            label=f'{revision}-{mode[2:]}-{trial}'
            cmd=[binary, assets,str(out/label),mode]
            result=subprocess.run(cmd,env=env,text=True,capture_output=True,check=True)
            (out/f'{label}.log').write_text(result.stdout+result.stderr)
            assert 'gl_vendor=NVIDIA Corporation' in result.stdout, result.stdout
            for path in ['immediate','atlas']:
                match=re.search(rf'{path} sprites=512 unique=(\d+) frames=40 mean_ms=([\d.]+) draws_per_frame=(\d+)',result.stdout)
                assert match, result.stdout
                records.append(dict(mode=mode,trial=trial,revision=revision,path=path,unique=int(match[1]),mean_ms=float(match[2]),draws=int(match[3])))
summary=[]
for mode in ['--benchmark','--benchmark-pages']:
    for path in ['immediate','atlas']:
        row={'mode':mode,'path':path}
        for rev in ['old','new']:
            values=[r['mean_ms'] for r in records if (r['mode'],r['path'],r['revision'])==(mode,path,rev)]
            row[rev]={'median_ms':statistics.median(values),'min_ms':min(values),'max_ms':max(values),'runs':len(values)}
        row['change_percent']=100*(row['new']['median_ms']/row['old']['median_ms']-1)
        summary.append(row)
(out/'results.json').write_text(json.dumps({'records':records,'summary':summary,'environment':{'display':':0','SDL_VIDEO_DRIVER':'x11','__GL_SYNC_TO_VBLANK':'0'},'binary_sha256':hashlib.sha256(Path(binary).read_bytes()).hexdigest()},indent=2)+'\n')
print(json.dumps(summary,indent=2),flush=True)
for pattern in ['white','stripes','spots','patch']:
    folder=out/pattern
    folder.mkdir(exist_ok=True)
    result=subprocess.run([binary,f'artifacts/skins/native/{pattern}',str(folder/'review'),'--all-phases'],env=env,text=True,capture_output=True,check=True)
    (out/f'{pattern}.log').write_text(result.stdout+result.stderr)
    print(f'{pattern}: {len(list(folder.glob("*.bmp")))} capture pages',flush=True)
for check in ['--validate-cache','--validate-opacity']:
    result=subprocess.run([binary,'artifacts/skins/native/white',str(out/check[2:]),check],env=env,text=True,capture_output=True,check=True)
    (out/f'{check[2:]}.log').write_text(result.stdout+result.stderr)
    print(result.stdout,flush=True)
