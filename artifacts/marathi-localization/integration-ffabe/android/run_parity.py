"""Actual owned Android CPU parity: invoke only after final official build+installed QA."""
from pathlib import Path
import argparse,json,hashlib,subprocess,shlex,shutil,tempfile,uuid,os,time,zipfile
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-ffabe/android';parity=base/'parity';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=[str(sdk/'platform-tools/adb'),'-P','15187','-s','emulator-5678'];env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
p=argparse.ArgumentParser();p.add_argument('--expected-revision',required=True);p.add_argument('--native-trace',type=Path,required=True);p.add_argument('--wasm-trace',type=Path);args=p.parse_args()
def output(cmd):return subprocess.check_output(cmd,env=env,text=True,timeout=60)
def run(cmd,**kw):return subprocess.run(cmd,env=env,check=True,**kw)
assert output(['git','rev-parse','HEAD']).strip()==args.expected_revision and not output(['git','status','--porcelain']).strip()
owned=json.loads((base/'owned-runtime.json').read_text());inspect=json.loads(output(['docker','inspect',owned['container_id']]))[0];assert inspect['Id']==owned['container_id'] and inspect['Name']=='/glob2-marathi-emulator';assert owned['adb_port']==15187 and owned['serial']=='emulator-5678';assert output(adb+['shell','getprop','ro.boot.qemu.avd_name']).strip()=='glob2-api35-x64';assert output(adb+['shell','getprop','persist.sys.locale']).strip()=='mr-IN'
proof=json.loads((parity/'build-provenance.json').read_text());assert proof['expected_clean_source']==args.expected_revision and proof['status'].startswith('verified ignored');exe=Path(proof['binary']);assert sha(exe)==proof['binary_sha256'];producer=json.loads((base/'producer-provenance.json').read_text());assert producer['state']=='verified frozen shipping official build';assert set(output(['git','diff','--name-only',producer['compiled_shipping_revision'],args.expected_revision]).splitlines())<={'test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt'}
fixture=root/'test/fixtures/multiplayer/FourSquares1.g2mr';mapfile=root/'maps/FourSquares1.map.gz';golden=root/'test/fixtures/multiplayer/FourSquares1.verify-trace.txt';assert args.native_trace.resolve()!=golden.resolve() and args.native_trace.is_file();assert args.native_trace.read_bytes().replace(b'\r\n',b'\n')==golden.read_bytes().replace(b'\r\n',b'\n')
remote='/data/local/tmp/glob2-marathi-parity-'+uuid.uuid4().hex;assert subprocess.run(adb+['shell','test','-e',remote],env=env).returncode!=0
result={'source_revision':args.expected_revision,'compiled_engine_shipping_revision':producer['compiled_shipping_revision'],'mode':'Actual Androidx86_64CPU on owned emulator; ignored PIElinkedexactofficialengineobjects withNativeTestMainbridges. Not APK/JVM/physicalhardware.','serial':'emulator-5678','adb_port':15187,'owned_container_id':owned['container_id'],'remote_directory':remote,'binary_sha256':sha(exe),'build_provenance_sha256':sha(parity/'build-provenance.json'),'fixture_sha256':sha(fixture),'map_sha256':sha(mapfile),'committed_golden_sha256':sha(golden),'payload_cap_bytes':512*1024**2,'output_cap_bytes':64*1024**2,'run_timeout_seconds':600,'remote_retained':True,'status':'preparing'}
def save():(parity/'device-result.json').write_text(json.dumps(result,indent=2)+'\n')
nativeproof=root/'artifacts/marathi-localization/integration-ffabe/native/authoritative-native-trace.json'
np=json.loads(nativeproof.read_text());assert np['record_sha256']==sha(fixture) and np['trace_sha256']==sha(args.native_trace) and np['actual_tick_samples']==842
result['independent_native_producer_proof']={'path':str(nativeproof),'sha256':sha(nativeproof),'actual_native_binary_sha256':np['actual_binary_sha256'],'actual_native_cli_build_revision':np['actual_official_cli_build']['actual_official_build_revision']}
save()
with tempfile.TemporaryDirectory(prefix='glob2-mr-parity-') as temp:
 payload=Path(temp)/'payload';payload.mkdir();assets=root/'build/android/device/x86_64/24/client/release/runtime-assets';shutil.copytree(assets,payload,dirs_exist_ok=True);shutil.copy2(exe,payload/'android-match-verify');shutil.copy2(fixture,payload/'match.g2mr');shutil.copy2(mapfile,payload/'FourSquares1.map.gz')
 with zipfile.ZipFile(producer['apk']['path']) as z:
  for name in z.namelist():
   if name.startswith('lib/x86_64/') and name.endswith('.so') and not name.endswith('/libmain.so'):(payload/Path(name).name).write_bytes(z.read(name))
 hashes={str(f.relative_to(payload)):sha(f) for f in payload.rglob('*') if f.is_file()};size=sum(f.stat().st_size for f in payload.rglob('*') if f.is_file());assert size<=result['payload_cap_bytes'];assert shutil.disk_usage(root).free>=2*1024**3
 (parity/'device-inputs.json').write_text(json.dumps({'sha256':hashes,'payload_bytes':size},indent=2)+'\n');result['payload_bytes']=size;result['device_inputs_sha256']=sha(parity/'device-inputs.json');save()
 with (parity/'push.log').open('w') as log:run(adb+['push',str(payload),remote],stdout=log,stderr=subprocess.STDOUT,timeout=180)
# Exact transferred input hashes are measured on-device, never inferred frompush exit.
listing=output(adb+['shell','cd '+shlex.quote(remote)+' && find . -type f -exec sha256sum {} \\;']);(parity/'on-device-inputs.txt').write_text(listing);actual={}
for line in listing.splitlines():
 digest,filename=line.split(None,1);filename=filename.removeprefix('./');actual[filename]=digest
assert actual==hashes,'Actual staged Androidinputs differ fromsourcepayload'
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
