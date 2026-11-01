from pathlib import Path
import argparse,json,hashlib,subprocess,shlex,os,time
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-ffabe/android';parity=base/'parity';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=[str(sdk/'platform-tools/adb'),'-P','15187','-s','emulator-5678'];env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
p=argparse.ArgumentParser();p.add_argument('--expected-revision',required=True);p.add_argument('--native-trace',type=Path,required=True);p.add_argument('--wasm-trace',type=Path);args=p.parse_args()
def output(cmd):return subprocess.check_output(cmd,env=env,text=True,timeout=120)
def run(cmd,**kw):return subprocess.run(cmd,env=env,check=True,**kw)
assert output(['git','rev-parse','HEAD']).strip()==args.expected_revision and not output(['git','status','--porcelain']).strip()
owned=json.loads((base/'owned-runtime.json').read_text());result=json.loads((parity/'attempt1-device-result.json').read_text());remote=result['remote_directory'];assert remote.startswith('/data/local/tmp/glob2-marathi-parity-') and len(remote.rsplit('-',1)[1])==32
assert json.loads(output(['docker','inspect',owned['container_id']]))[0]['Id']==result['owned_container_id'];assert output(adb+['shell','getprop','ro.boot.qemu.avd_name']).strip()=='glob2-api35-x64';assert output(adb+['shell','getprop','persist.sys.locale']).strip()=='mr-IN'
assert subprocess.run(adb+['shell','test','-e',remote+'/output'],env=env).returncode!=0,'Never resume over existing simulationoutput'
fixture=root/'test/fixtures/multiplayer/FourSquares1.g2mr';mapfile=root/'maps/FourSquares1.map.gz';golden=root/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt';assert sha(fixture)==result['fixture_sha256'] and sha(golden)==result['committed_golden_sha256'];assert sha(args.native_trace)==result['committed_golden_sha256']
hashes=json.loads((parity/'device-inputs.json').read_text())['sha256'];assert len(hashes)==24345
# Batchedfindusesliteralplus terminator; no shellescapedsemicolon ambiguity oroneforkperfile.
listing=output(adb+['shell','cd '+shlex.quote(remote)+' && find . -type f -exec sha256sum {} +']);(parity/'on-device-inputs.txt').write_text(listing);actual={}
for line in listing.splitlines():
 digest,filename=line.split(None,1);actual[filename.removeprefix('./')]=digest
assert actual==hashes,'Everyactualstagedinputhashmustmatchoriginalpayload'
result['input_hash_resume']={'all_hashes_verified':len(actual),'method':'Actualbatchedfind-execsha256sum{}+, all files verified; originalpayloadreusedwithoutrepush.','prior_failed_attempt':'attempt1-input-hash-timeout.json','runner_sha256':sha(base/'resume_parity.py')}
def save():(parity/'device-result.json').write_text(json.dumps(result,indent=2)+'\n')
save()
# Own output must be fresh. Standard Android timeout bounds remote execution even
# if the host-sideADBclient is interrupted. ulimit bounds each created outputfile.
command='cd '+shlex.quote(remote)+' && chmod 755 android-match-verify && ulimit -f 65536 && '+shlex.join(['env','LD_LIBRARY_PATH='+remote,'SDL_VIDEODRIVER=dummy','SDL_AUDIODRIVER=dummy','SDL_RENDER_DRIVER=software','GLOB2_ASSET_DIR='+remote,'GLOB2_USER_DIR='+remote+'/profile','GLOB2_USER_DATA_DIR='+remote+'/profile','timeout','-k','5','600','./android-match-verify','match','verify',remote+'/match.g2mr','--map-file',remote+'/FourSquares1.map.gz','--output-dir',remote+'/output'])
result['invocation']=command;result['status']='running actual Android simulation';save()
with (parity/'device.log').open('w') as log:
 process=subprocess.Popen(adb+['shell',command],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);start=time.monotonic()
 while process.poll() is None:
  if time.monotonic()-start>620 or log.tell()>8*1024**2:
   os.killpg(process.pid,15);process.wait(timeout=10);result['status']='failed ownedremote deadline or8MiBlogcap';save();raise RuntimeError(result['status'])
  time.sleep(.2)
 result['exit_code']=process.returncode
save();assert result['exit_code']==0,'Android verifier failed; retained ownremote/log'
kb=int(output(adb+['shell','du','-sk',remote+'/output']).split()[0]);assert kb*1024<=result['output_cap_bytes'],'Actual verifieroutput exceeds explicit64MiBcap'
local=parity/'device-output';assert not local.exists()
with (parity/'pull.log').open('w') as log:run(adb+['pull',remote+'/output',str(local)],stdout=log,stderr=subprocess.STDOUT,timeout=120)
trace=(local/'checksums.txt').read_bytes().replace(b'\r\n',b'\n');expected=golden.read_bytes().replace(b'\r\n',b'\n');verdict=json.loads((local/'verdict.json').read_text());assert verdict['verdict']=='verified';assert trace==expected
lines=[x for x in trace.decode().splitlines() if x and not x.startswith('#')];ticks=[int(x.split()[0]) for x in lines];assert len(ticks)>600 and ticks==list(range(len(ticks)));assert trace==args.native_trace.read_bytes().replace(b'\r\n',b'\n')
result.update({'status':'PASS Android completeper-ticktrace equalscommittedgoldenandindependentnative','tick_count':len(ticks),'first_tick':ticks[0],'last_tick':ticks[-1],'trace_sha256':hashlib.sha256(trace).hexdigest(),'verdict':verdict,'native_trace':str(args.native_trace),'native_trace_sha256':sha(args.native_trace),'full_trace_equal_native':True,'full_trace_equal_committed_golden':True,'WASM_comparison':'pending independentcurrentWASMtrace' if not args.wasm_trace else 'performed'})
if args.wasm_trace:
 assert trace==args.wasm_trace.read_bytes().replace(b'\r\n',b'\n');result.update({'wasm_trace':str(args.wasm_trace),'wasm_trace_sha256':sha(args.wasm_trace),'full_trace_equal_WASM':True})
save()
assert remote.startswith('/data/local/tmp/glob2-marathi-parity-') and len(remote.rsplit('-',1)[1])==32
run(adb+['shell','rm','-rf',remote]);assert subprocess.run(adb+['shell','test','-e',remote],env=env).returncode!=0;result['remote_retained']=False;result['owned_remote_cleanup_complete']=True;save();print(result['status'],len(ticks),'ticks')
