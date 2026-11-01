from pathlib import Path
import argparse,os,json,hashlib,subprocess,time,shutil
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-refresh/native'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--official',type=Path,required=True);ap.add_argument('--adapter',type=Path,required=True);a=ap.parse_args();out=base/'captures';out.mkdir(exist_ok=True);assert not (out/'runner.json').exists(),'Retain previous attempt; choose a new output name before rerunning'
 binaries={'official':a.official.resolve(),'adapter':a.adapter.resolve()};identities={k:{'path':str(v),'sha256':sha(v)} for k,v in binaries.items()};source=json.loads((base/'previews/mr/metadata.json').read_text());assert source['source_revision']=='ffbd2aa743dad5f6b9b71bd8cc64f5c6d94d73c4';assert source['original_catalog_sha256']['native']==sha(root/'data/texts.mr.txt')
 record={'source':source,'binaries':identities,'capture_wrapper_source':json.loads((base/'capture-wrapper-source.json').read_text()),'jobs':[],'evidence_byte_cap':1024**3,'minimum_free_bytes':2*1024**3,'deadlines_seconds':{'matrix':600,'screen':240,'tutorial':300,'gameplay_with_results_fixture':600},'limits':['Current production native/harness renderer; full-value overlay equals registered 3395 native values.','Results screen is a rendering fixture from a loaded game with real automatically generated probability samples, not a played/completed match.','Phone post-game metric dropdown is distinct from live compact stats, which deliberately exclude win probability.','Old 75072 samples are carried only through explicit unchanged font/wrap input proof; only the two new strings are sampled afresh here.']};write(out/'runner.json',record)
 env=dict(os.environ,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11');env['LD_LIBRARY_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','');rfd,wfd=os.pipe();log=(out/'xvfb.log').open('w');xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wfd),'-screen','0','1600x1200x24','-nolisten','tcp'],pass_fds=[wfd],stdout=subprocess.DEVNULL,stderr=log);os.close(wfd)
 try:
  with os.fdopen(rfd) as f:display=f.readline().strip()
  assert display;env['DISPLAY']=':'+display
  for w,h in [(1280,900),(390,844)]:
   for name,mode,kind,producer,inputs in [('new2-font','matrix',None,'official',base/'new2-previews/mr'),('main','screen','main','official',base/'previews/mr'),('settings','screen','settings','official',base/'previews/mr'),('gameplay-results','game','gameplay','adapter',base/'previews/mr'),('tutorial-introduction','game','tutorial:introduction','official',base/'previews/mr')]:
    dest=out/f'{w}x{h}'/name;dest.mkdir(parents=True,exist_ok=True);profile=out/'profiles'/f'{w}x{h}'/name;jobenv=dict(env,GLOB2_USER_DATA_DIR=str(profile),GLOB2_MOBILE_UI='touch-auto' if w==390 else '0',XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'));assert sha(binaries[producer])==identities[producer]['sha256'];assert sha(root/'data/texts.mr.txt')==source['original_catalog_sha256']['native'];command=[str(binaries[producer]),mode,str(root),str(inputs),str(dest),str(w),str(h)]+([kind] if kind else []);start=time.monotonic();deadline=600 if kind=='gameplay' or mode=='matrix' else 300 if mode=='game' else 240
    with (dest/'capture.log').open('w') as stream:
     child=subprocess.Popen(command,env=jobenv,stdout=stream,stderr=subprocess.STDOUT)
     try:
      while child.poll() is None:
       if time.monotonic()-start>deadline:raise RuntimeError('Bounded validation deadline exceeded')
       if sum(p.stat().st_size for p in out.rglob('*') if p.is_file())>record['evidence_byte_cap']:raise RuntimeError('Evidence cap exceeded')
       if shutil.disk_usage(out).free<record['minimum_free_bytes']:raise RuntimeError('Free-space reserve reached')
       time.sleep(1)
     except BaseException:
      child.terminate();child.wait(timeout=10);raise
    job={'viewport':[w,h],'name':name,'mode':mode,'kind':kind,'producer':producer,'command':command,'returncode':child.returncode,'seconds':round(time.monotonic()-start,2),'output':str(dest.relative_to(out))};record['jobs'].append(job);write(out/'runner.json',record);print(job,flush=True);assert child.returncode==0,'Capture failed; retain log/partial evidence'
 finally:xvfb.terminate();xvfb.wait(timeout=10);log.close()
 print('All 10 integration capture jobs completed',flush=True)
if __name__=='__main__':main()
