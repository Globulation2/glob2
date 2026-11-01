from pathlib import Path
import json,subprocess,os,time,hashlib,shutil
root=Path.cwd();b=root/'artifacts/marathi-localization/integration-bc8/native-registration187';out=b/'captures';out.mkdir(exist_ok=True);assert not (out/'runner.json').exists();binary=b/'RegisteredPreviewHarness-stripped';proof=json.loads((b/'variant-build-and-strip.json').read_text());expected=proof['strip_proofs']['registered_variant']['executed_copy']['sha256'];assert hashlib.sha256(binary.read_bytes()).hexdigest()==expected;resource=root/'data/texts.incomplete.txt';resource_hash=hashlib.sha256(resource.read_bytes()).hexdigest();assert resource_hash=='6cce20e5254acfafb5d43ff29994b7c510265e031510da777a95d98e7a97599a';changes=subprocess.check_output(['git','diff','--name-only','187ea34de87838911a42321fd52925d51da475b9..HEAD'],text=True).splitlines();assert changes==['browser/tests/marathi.spec.js']
record={'actual_compiled_producer':'187ea34de87838911a42321fd52925d51da475b9','current_test_only_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'test_only_changes_since_producer':changes,'binary_sha256':expected,'registered_resource_sha256':resource_hash,'variant_source':'../registered-variant-source.json','jobs':[],'byte_cap':128*1024**2,'minimum_free_bytes':2*1024**3,'per_job_deadline_seconds':300,'scope':'Actual normal registered texts.list/incomplete/nativeMR resources; capture-only variant bypasses all draft overlays and asserts complete MR plus correct Player selector label. Generic inherited overlay disclaimers in raw harness limit prose are superseded by the explicit registered_resource_probe and overridden_keys=0; no full locale re-review/rerender claim after the metadata-only followup.'}
def write(): (out/'runner.json').write_text(json.dumps(record,indent=2)+'\n')
write();env=dict(os.environ,SDL_AUDIODRIVER='dummy',SDL_VIDEO_DRIVER='x11',SDL_VIDEODRIVER='x11',XDG_SESSION_TYPE='x11',GLOB2_CAPTURE_REGISTERED='1');env.pop('WAYLAND_DISPLAY',None);env['LD_LIBRARY_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','');rfd,wfd=os.pipe();xlog=(out/'xvfb.log').open('w');xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wfd),'-screen','0','1600x1200x24','-nolisten','tcp'],pass_fds=[wfd],stdout=subprocess.DEVNULL,stderr=xlog);os.close(wfd)
try:
 with os.fdopen(rfd) as f:display=f.readline().strip()
 assert display;env['DISPLAY']=':'+display
 for w,h in [(1280,900),(390,844)]:
  for screen in ['main','settings-player']:
   dest=out/f'{w}x{h}'/screen;dest.mkdir(parents=True,exist_ok=True);profile=out/'profiles'/f'{w}x{h}'/screen;jobenv=dict(env,GLOB2_MOBILE_UI='touch-auto' if w==390 else '0',GLOB2_USER_DATA_DIR=str(profile),XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'));assert hashlib.sha256(resource.read_bytes()).hexdigest()==resource_hash;command=[str(binary),'screen',str(root),str(b/'previews/mr'),str(dest),str(w),str(h),screen];start=time.monotonic()
   with (dest/'capture.log').open('w') as log:
    child=subprocess.Popen(command,env=jobenv,stdout=log,stderr=subprocess.STDOUT)
    try:
     while child.poll() is None:
      assert time.monotonic()-start<300,'Registered capture deadline reached';assert sum(p.stat().st_size for p in out.rglob('*') if p.is_file())<record['byte_cap'];assert shutil.disk_usage(out).free>record['minimum_free_bytes'];time.sleep(1)
    except BaseException:child.terminate();child.wait(timeout=10);raise
   job={'viewport':[w,h],'screen':screen,'command':command,'returncode':child.returncode,'seconds':round(time.monotonic()-start,2),'output':str(dest.relative_to(out))};record['jobs'].append(job);write();assert child.returncode==0,'Registered capture failed';d=json.loads((dest/(screen+'.json')).read_text());probe=d['evidence_identity']['registered_resource_probe'];assert probe['complete'] and probe['native_name']=='मराठी' and probe['overlay_keys_overridden']==0 and d['native_keys_overridden']==0;print(job,flush=True)
finally:xvfb.terminate();xvfb.wait(timeout=10);xlog.close()
print('All4 registered187 native capture jobs passed',flush=True)
