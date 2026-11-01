from pathlib import Path
import argparse,os,json,hashlib,subprocess,time,shutil
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--official',type=Path,required=True);ap.add_argument('--adapter',type=Path,required=True);a=ap.parse_args();out=base.parent/'native-extra-clean64'/'refined';out.mkdir(exist_ok=True);assert not (out/'runner.json').exists(),'Retain previous attempt; choose a new output name before rerunning'
 binaries={'official':a.official.resolve(),'adapter':a.adapter.resolve()};identities={k:{'path':str(v),'sha256':sha(v)} for k,v in binaries.items()};source=json.loads((base.parent/'previews/mr/metadata.json').read_text());assert source['source_revision']=='64aeda1300e3fcb81b074aef1a9a685498edcf63';assert source['original_catalog_sha256']['native']==sha(root/'data/texts.mr.txt')
 record={'display_environment':{'SDL_VIDEO_DRIVER':'x11','SDL_VIDEODRIVER':'x11','XDG_SESSION_TYPE':'x11','ownership':'Runner-owned Xvfb, replacing inherited desktop DISPLAY'},'source':source,'binaries':identities,'capture_wrapper_source':json.loads((base/'extra-capture-v2-source.json').read_text()),'jobs':[],'evidence_byte_cap':2*1024**3,'minimum_free_bytes':2*1024**3,'deadlines_seconds':{'matrix':600,'screen':240,'tutorial':300,'gameplay_with_results_fixture':600},'limits':['Current production native/harness renderer; full-value overlay equals registered 3411 native values.','Results screen is a production rendering fixture from a loaded game; no sampled values are injected and no match completion is claimed.','Phone post-game metric dropdown is distinct from live compact stats, which deliberately exclude win probability.','Fresh full current matrix is recorded separately in native-render-clean64; no stale font measurement carry is claimed.']};write(out/'runner.json',record)
 env=dict(os.environ,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',SDL_VIDEO_DRIVER='x11',XDG_SESSION_TYPE='x11');env['LD_LIBRARY_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','');rfd,wfd=os.pipe();log=(out/'xvfb.log').open('w');xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wfd),'-screen','0','1600x1200x24','-nolisten','tcp'],pass_fds=[wfd],stdout=subprocess.DEVNULL,stderr=log);os.close(wfd)
 try:
  with os.fdopen(rfd) as f:display=f.readline().strip()
  assert display;env['DISPLAY']=':'+display
  for w,h in [(1280,900),(390,844)]:
   for name,mode,kind,producer,inputs in [('gameplay-extra','game','gameplay','adapter',base.parent/'previews/mr')]:
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
 print('Both extra UI capture jobs completed',flush=True)
if __name__=='__main__':main()
