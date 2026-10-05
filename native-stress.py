from pathlib import Path
import subprocess,os,time,json,sys
root=Path.cwd(); seconds=600; competitors=[]; tests=[]
burn='import math,time; end=time.monotonic()+620; x=1\nwhile time.monotonic()<end:\n for i in range(10000): x=math.sqrt(x+i)'
memory='import ctypes,time; data=bytearray(128*1024*1024); address=ctypes.addressof(ctypes.c_char.from_buffer(data)); end=time.monotonic()+620; n=0\nwhile time.monotonic()<end: ctypes.memset(address,n%256,len(data)); n+=1'
cpus=sorted(os.sched_getaffinity(0)); single=cpus[-1]
env={**os.environ,'GLOB2_AUDIO_THREAD_PRIORITY':'0','GLOB2_AUDIO_STRESS_SECONDS':str(seconds)}
exe=root/'build/linux/client/release/test/glob2-engine-tests'
try:
 for _ in range(2):competitors.append(subprocess.Popen(['taskset','-c',str(single),sys.executable,'-c',burn],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
 for _ in cpus:competitors.append(subprocess.Popen(['nice','-n','10',sys.executable,'-c',burn],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
 for _ in range(4):competitors.append(subprocess.Popen(['nice','-n','10',sys.executable,'-c',memory],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
 for mode in ('singlecore','allcore','memory'):
  log=open(root/f'artifacts/audio/native-stress-{mode}.log','w')
  cmd=(['taskset','-c',str(single)] if mode=='singlecore' else [])+[str(exe),'--test-case=native music supply*']
  tests.append((mode,subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT),log))
 results={mode:proc.wait() for mode,proc,_ in tests}
 Path('artifacts/audio/native-stress-results.json').write_text(json.dumps({'seconds':seconds,'cpus':cpus,'singleCore':single,'ordinaryProducerPriority':True,'concurrentLoads':{'singleCoreCompetitors':2,'cpuCompetitorsNice10':len(cpus),'memoryWorkersNice10':4,'memoryMiBPerWorker':128},'exitCodes':results},indent=2)+'\n')
 if any(results.values()):sys.exit(1)
finally:
 for proc in competitors:
  if proc.poll() is None:proc.terminate()
 for proc in competitors:proc.wait()
 for _,proc,log in tests:
  if proc.poll() is None:proc.terminate();proc.wait()
  log.close()
