import os
import json,pathlib,sys,math,statistics,random
from measure import OUT,ROOT,run
ASSETS=ROOT/'build/linux/client/release/client-full-direct-1111/runtime-assets'
from paired_stats import confidence
variants=sys.argv[1:] or ['a','b','ab']
summary={}
for variant in variants:
    summary[variant]={}
    for scenario in ['startup','reload']:
        pairs=[]
        for repeat in range(21):
            values={}
            for name in (['baseline',variant] if repeat%2==0 else [variant,'baseline']):
                binary=OUT/(os.environ.get('TRIAL_BASELINE','baseline') if name=='baseline' else name)/'asset-loading-benchmark'
                env={'GLOB2_ASSET_BENCHMARK_GPU':'1','GLOB2_ISOLATED_TRIAL':'1','GLOB2_PROFILE_GPU':'1','GLOB2_PROFILE_GPU_INTERVAL':'1'}
                env['GLOB2_ASSET_BENCHMARK_HD' if scenario=='startup' else 'GLOB2_ASSET_BENCHMARK_RELOAD']='1'
                result=run(f'asset-{variant}-{scenario}-{repeat:02}-{name}',[str(binary),str(ASSETS)],env)
                phase=next(row for row in result['asset'] if row['phase']==scenario)
                values[name]={'rss_kib':result['kernel_peak_rss_kib'],'cpu_ms':float(phase['cpu_ms']),'ready_ms':float(phase['ready_ms']),'fingerprint':phase['fingerprint'],'hd_cpu_bytes':int(phase['hd_cpu_bytes']),'texture_bytes':int(phase['texture_bytes']),'sampled_texture_peak_bytes':max((row.get('texture_bytes',0) for row in result['samples']),default=0),'sampled_texture_peak_count':max((row.get('texture_count',0) for row in result['samples']),default=0),'sampled_vram_peak_mib':max((sum(int(process.get('used_memory','0 MiB').split()[0]) for process in sample['processes']) for sample in result['gpu_samples']),default=0)}
            for key in ['fingerprint','hd_cpu_bytes','texture_bytes']:
                if values['baseline'][key]!=values[variant][key]:raise RuntimeError(f'{variant}/{scenario}: mismatch {key}: {values}')
            pairs.append(values)
            gates={key:confidence([row[variant][key]/row['baseline'][key] for row in pairs]) for key in ['cpu_ms','ready_ms','rss_kib']}
            summary[variant][scenario]={'pairs':pairs,'gates':gates,'memory_reduction_percent':100*(1-statistics.median(row[variant]['rss_kib'] for row in pairs)/statistics.median(row['baseline']['rss_kib'] for row in pairs))}
            (OUT/os.environ.get('TRIAL_REPORT','asset-comparison.json')).write_text(json.dumps(summary,indent=2))
            print(json.dumps({'variant':variant,'scenario':scenario,'repeat':repeat,'gates':gates}),flush=True)
            if len(pairs)>=7 and all(gates[key]['upper95_percent']<=2 for key in ['cpu_ms','ready_ms']):break
