import argparse, gzip, json, os, signal, subprocess, threading, time
from pathlib import Path
import shutil
p=argparse.ArgumentParser()
p.add_argument('--label',required=True)
p.add_argument('--timeout',type=int,default=1800)
p.add_argument('command',nargs=argparse.REMAINDER)
a=p.parse_args()
command=a.command[1:] if a.command[:1]==['--'] else a.command
root=Path.cwd()
out=root/'artifacts/terrain-decoration-pr/linux'
out.mkdir(parents=True,exist_ok=True)
start=time.monotonic()
revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
meta={'label':a.label,'revision':revision,'command':command,'cwd':str(root),'timeout_seconds':a.timeout,
      'environment':{k:os.environ.get(k) for k in ['GLOB2_SDL3_PREFIX','PKG_CONFIG_PATH','LD_LIBRARY_PATH','GLOB2_REQUIRE_OPENCL','GLOB2_REQUIRE_VULKAN']}}
reason=[]
if shutil.disk_usage(out).free<2*1024**3:
    meta.update(exit=None,error='free-space budget');(out/(a.label+'.json')).write_text(json.dumps(meta,indent=2));raise SystemExit(2)
proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
def stop(why):
    if proc.poll() is None:
        reason.append(why)
        try:os.killpg(proc.pid,signal.SIGKILL)
        except ProcessLookupError:pass
def guard():
    while proc.poll() is None:
        if time.monotonic()-start>a.timeout:stop('timeout')
        elif shutil.disk_usage(out).free<2*1024**3:stop('free-space budget')
        total=sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
        if total>8*1024**3:stop('artifact budget')
        time.sleep(5)
threading.Thread(target=guard,daemon=True).start()
size=0
with gzip.open(out/(a.label+'.log.gz'),'wb') as log:
    for line in proc.stdout:
        size+=len(line)
        if size>16*1024**2:
            stop('log budget')
            break
        log.write(line)
code=proc.wait()
meta.update(exit=code,elapsed_seconds=time.monotonic()-start,output_bytes=size,termination=reason)
(out/(a.label+'.json')).write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps(meta),flush=True)
raise SystemExit(0 if code==0 and not reason else 1)
