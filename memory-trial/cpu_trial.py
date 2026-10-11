import os, pathlib, time

def partition():
    group=pathlib.Path('/proc/self/cgroup').read_text().strip()
    if not group.startswith('0::/glob2-benchmark-'):
        return None
    path=pathlib.Path('/sys/fs/cgroup')/group[4:]
    reserved=path.joinpath('cpuset.cpus').read_text().strip()
    return {'cgroup':group,'path':str(path),'engine_cpus':sorted(os.sched_getaffinity(0)),
            'reserved_cpus':reserved,'partition':path.joinpath('cpuset.cpus.partition').read_text().strip()}

def competitors():
    own=partition()
    found=[]
    for status in pathlib.Path('/proc').glob('[0-9]*/comm'):
        try:
            name=status.read_text().strip()
            browser=False
            if name in ['node','npm exec playwr']:
                browser=b'playwright' in status.with_name('cmdline').read_bytes()
            if browser or name in ['scons','cc1','cc1plus','glob2','glob2-engine-te','glob2-unit-test','AssetLoadingBen','asset-loading-b','torus-render-be'] or name.startswith('clang-'):
                # The exclusive partition prevents work outside it from using
                # either the benchmark cores or their deliberately idle siblings.
                if own and status.with_name('cgroup').read_text().strip()!=own['cgroup']:
                    continue
                found.append((int(status.parent.name),name))
        except OSError:pass
    return found

def snapshot():
    busy={}
    for line in pathlib.Path('/proc/stat').read_text().splitlines():
        fields=line.split()
        if fields and fields[0].startswith('cpu') and fields[0][3:].isdigit():
            values=list(map(int,fields[1:9]))
            busy[fields[0]]=sum(value for i,value in enumerate(values) if i not in (3,4))
    frequencies={}
    for cpu in sorted(os.sched_getaffinity(0)):
        values={}
        for key in ['scaling_cur_freq','scaling_governor']:
            try:values[key]=pathlib.Path(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq/{key}').read_text().strip()
            except OSError:values[key]=None
        frequencies[str(cpu)]=values
    return {'monotonic_seconds':time.monotonic(),'busy_jiffies':busy,
            'clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'frequency_boundary':frequencies}

def activity(before,after,child_cpu):
    elapsed=after['monotonic_seconds']-before['monotonic_seconds']
    ticks=before['clock_ticks_per_second']
    deltas={cpu:(value-before['busy_jiffies'][cpu])/ticks for cpu,value in after['busy_jiffies'].items()}
    engine=sum(deltas[f'cpu{cpu}'] for cpu in os.sched_getaffinity(0))
    return {'snapshot_elapsed_seconds':elapsed,'per_cpu_busy_seconds':deltas,
            'engine_busy_seconds':engine,'child_cpu_seconds':child_cpu,
            'approximate_other_engine_cpu_seconds':engine-child_cpu,
            'note':'Coarse boundary /proc/stat counters; includes runner, interrupts and kernel activity. Other host work is confined outside the reserved physical cores. Frequency is a boundary snapshot, not an interval measurement.'}
