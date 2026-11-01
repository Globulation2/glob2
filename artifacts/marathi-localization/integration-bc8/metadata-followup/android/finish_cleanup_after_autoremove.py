from pathlib import Path
import subprocess,json,os,time,hashlib
root=Path.cwd();b=root/'artifacts/marathi-localization/integration-bc8/metadata-followup/android';sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=sdk/'platform-tools/adb';env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
def run(args,**kw):return subprocess.run([str(x) for x in args],env=env,**kw)
def output(args):return subprocess.check_output([str(x) for x in args],env=env,text=True)
record=json.loads((b/'runtime-cleanup.json').read_text());cid=record['ownership_verified']['container_id'];server=record['ownership_verified']['adb_server_pid'];proc=Path(f'/proc/{server}');expected=record['ownership_verified']['adb_server_command'];preserved=record['preserved_apks_sha256'];sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert cid==json.loads((b/'owned-runtime.json').read_text())['container_id']
assert run(['docker','inspect',cid],capture_output=True).returncode!=0
record['cleanup_adaptation']={'original_attempt':'docker stop returned0, but immediate inspect assertion raced Docker --rm asynchronous removal; original script exited before ADB cleanup.','followup':'Independent later inspect confirmed exactownedCIDabsent; continuation rechecks savedownership+exactADB PIDcommand/cwd beforekill.','container_stop_exit_code_observed':0}
assert (proc/'cwd').resolve()==root
assert proc.exists() and [a.decode() for a in (proc/'cmdline').read_bytes().split(b'\0') if a]==expected
remaining=output([adb,'-P','15187','devices','-l']);assert all(l.split()[0]=='emulator-5678' for l in remaining.splitlines()[1:] if l.strip()),remaining
command=[str(adb),'-P','15187','kill-server'];print('Stopping only verified isolated ADB server15187',flush=True);done=run(command,capture_output=True,text=True,timeout=15);assert done.returncode==0,(done.stdout,done.stderr);record['operations'].append({'command':command,'exit_code':done.returncode,'stdout':done.stdout,'devices_before_kill':remaining})
for _ in range(100):
 if not proc.exists():break
 time.sleep(0.1)
assert not proc.exists();listeners=output(['ss','-ltnp','sport = :15187']);assert len(listeners.strip().splitlines())==1,listeners
for p,digest in preserved.items():assert sha(p)==digest
assert (root/'build/mobile-tools/avd/glob2-api35-x64.avd/userdata-qemu.img').is_file();record.update({'container_removed_after_graceful_stop':True,'adb_server_gone':True,'adb_listener_after':listeners,'avd_files_preserved':str(root/'build/mobile-tools/avd/glob2-api35-x64.avd'),'reproducibility_artifacts_preserved':str(b),'apks_rehashed_unchanged_after_cleanup':True,'status':'owned runtime stopped; artifacts preserved'});(b/'runtime-cleanup.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS owned container and ADB stopped; AVD/APKs/all evidence preserved',flush=True)
