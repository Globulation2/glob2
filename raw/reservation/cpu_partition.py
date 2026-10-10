#!/usr/bin/python3
"""Local reversible benchmark CPU reservation; run setup/run/cleanup via sudo -n."""
import argparse, json, os, pwd, sys, time
from pathlib import Path
ROOT=Path('/sys/fs/cgroup')
GROUP=ROOT/'codex-gpu1045-benchmark'
CPUS='8-15,24-31'
def read(path):
    return path.read_text().strip()
def snapshot():
    names=['cpuset.cpus','cpuset.cpus.effective','cpuset.cpus.exclusive.effective','cpuset.cpus.partition','cpuset.mems.effective','cgroup.procs']
    return {'time_ns':time.time_ns(),'group':str(GROUP),'partition':{n:read(GROUP/n) for n in names if (GROUP/n).exists()},'outside':{str(p):read(p/'cpuset.cpus.effective') for p in [ROOT/'user.slice',ROOT/'system.slice',ROOT/'init.scope'] if (p/'cpuset.cpus.effective').exists()}}
def validate():
    if read(GROUP/'cpuset.cpus.partition')!='root' or read(GROUP/'cpuset.cpus.effective')!=CPUS:
        raise RuntimeError('benchmark partition is not the expected exclusive scheduling domain')
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=['setup','status','run','cleanup'])
    parser.add_argument('--uid',type=int,default=1000)
    parser.add_argument('--env-json')
    raw=sys.argv[1:]
    split=raw.index('--') if '--' in raw else len(raw)
    command=raw[split+1:] if split<len(raw) else []
    args=parser.parse_args(raw[:split])
    if args.action=='status': print(json.dumps(snapshot(),indent=2)); return
    if os.geteuid()!=0: raise RuntimeError('setup/run/cleanup require sudo -n')
    if args.action=='setup':
        if GROUP.exists(): raise RuntimeError('refusing to overwrite existing benchmark cgroup')
        if 'cpuset' not in read(ROOT/'cgroup.subtree_control').split(): raise RuntimeError('root cpuset delegation unavailable')
        GROUP.mkdir()
        try:
            (GROUP/'cpuset.mems').write_text('0')
            (GROUP/'cpuset.cpus').write_text(CPUS)
            (GROUP/'cpuset.cpus.exclusive').write_text(CPUS)
            (GROUP/'cpuset.cpus.partition').write_text('root')
            validate()
        except BaseException:
            (GROUP/'cpuset.cpus.partition').write_text('member'); GROUP.rmdir(); raise
        print(json.dumps(snapshot(),indent=2)); return
    validate()
    if args.action=='cleanup':
        if read(GROUP/'cgroup.procs'): raise RuntimeError('refusing cleanup while benchmark processes are alive')
        (GROUP/'cpuset.cpus.partition').write_text('member'); GROUP.rmdir()
        print('Removed benchmark partition; outside CPUs restored.'); return
    if not command: raise RuntimeError('run requires a command')
    environment=dict(os.environ)
    if args.env_json:
        updates=json.loads(Path(args.env_json).read_text())
        if not isinstance(updates,dict) or not all(isinstance(k,str) and isinstance(v,str) for k,v in updates.items()): raise RuntimeError('environment must contain string pairs')
        environment.update(updates)
    identity=pwd.getpwuid(args.uid)
    environment.update(HOME=identity.pw_dir,USER=identity.pw_name,LOGNAME=identity.pw_name)
    (GROUP/'cgroup.procs').write_text(str(os.getpid()))
    validate()
    os.initgroups(identity.pw_name,identity.pw_gid)
    os.setgid(identity.pw_gid); os.setuid(args.uid)
    os.execvpe(command[0],command,environment)
if __name__=='__main__': main()
