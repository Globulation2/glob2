import os,sys,pathlib,subprocess,time,json,hashlib,re
from measure import OUT,ROOT
ASSETS=ROOT/'build/linux/client/release/client-full-direct-1111/runtime-assets'
from cpu_trial import competitors, partition
summary={}
for name in sys.argv[1:] or ['baseline','a','b','ab']:
    summary[name]={}
    for phase in ['startup','reload']:
        d=OUT/f'latest-heap-{name}-{phase}';d.mkdir(exist_ok=True)
        binary=OUT/name/'asset-loading-benchmark'
        env=dict(os.environ,LD_LIBRARY_PATH=str(OUT/'lib/glob2'),SDL_VIDEODRIVER='x11',GLOB2_USER_DATA_DIR=str(d/'profile'),GLOB2_ASSET_BENCHMARK_GPU='1')
        for key in ['MALLOC_ARENA_MAX','GLOB2_EXPERIMENT_TEXTURE_DIR','GLOB2_ASSET_THREADS','GLOB2_ASSET_IO_THREADS','GLOB2_ASSET_MEMORY_MB','GLOB2_ASSET_BENCHMARK_HD','GLOB2_ASSET_BENCHMARK_RELOAD']:env.pop(key,None)
        env['GLOB2_ASSET_BENCHMARK_HD' if phase=='startup' else 'GLOB2_ASSET_BENCHMARK_RELOAD']='1'
        command=['/usr/bin/heaptrack','-o',str(d/'trace'),str(binary),str(ASSETS)]
        if not (d/'summary.txt').exists():
            waiting=False
            while competitors():
                if not waiting:print(f'{name}/{phase}: waiting for builds or games',flush=True);waiting=True
                time.sleep(1)
            (d/'profile').mkdir(exist_ok=True)
            (d/'command.json').write_text(json.dumps({'command':command,'benchmark_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'metadata':json.loads((OUT/name/'metadata.json').read_text()),'cpu_reservation':partition(),'condition':'separate heap instrumentation; no performance claims'},indent=2))
            with (d/'stdout.log').open('w') as out:subprocess.run(command,cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
            traces=list(d.glob('trace*.zst'))+list(d.glob('trace*.gz'))
            if len(traces)!=1:raise RuntimeError(traces)
            with (d/'summary.txt').open('w') as out:subprocess.run(['/usr/bin/heaptrack_print','-f',str(traces[0]),'-n','20','-s','3','--print-flamegraph',str(d/'peak.stacks'),'--flamegraph-cost-type','peak'],stdout=out,stderr=subprocess.STDOUT,check=True)
        text=(d/'summary.txt').read_text();log=(d/'stdout.log').read_text()
        result={'allocations':int(re.search(r'allocations:\s+(\d+)',log)[1]),'peak_heap':re.search(r'peak heap memory consumption: (.+)',text)[1],'peak_stack_bytes':sum(int(line.rsplit(' ',1)[1]) for line in (d/'peak.stacks').read_text().splitlines() if line.strip()),'fingerprints':re.findall(r'fingerprint=(\w+)',log)}
        summary[name][phase]=result
        (OUT/'latest-heap-comparison.json').write_text(json.dumps(summary,indent=2));print(name,phase,result,flush=True)
