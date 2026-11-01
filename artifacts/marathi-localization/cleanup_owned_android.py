from pathlib import Path
import subprocess,json,os,time,hashlib
root=Path.cwd();b=root/'artifacts/marathi-localization';sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/mobile/Linux-x86_64-8dbf18a4a97410f6b6a92b4a/android-sdk');adb=sdk/'platform-tools/adb';env=dict(os.environ);env.pop('ADB_SERVER_SOCKET',None);env.pop('ANDROID_SERIAL',None)
def run(args,**kw):return subprocess.run([str(x) for x in args],env=env,**kw)
def output(args):return subprocess.check_output([str(x) for x in args],env=env,text=True)
inspection=json.loads(output(['docker','inspect','glob2-marathi-emulator']))[0];cid=inspection['Id'];cmd=inspection['Config']['Cmd'];assert inspection['Name']=='/glob2-marathi-emulator';assert cmd[0]=='chroot';assert cmd[cmd.index('-avd')+1]=='glob2-api35-x64';assert cmd[cmd.index('-port')+1]=='5678';assert 'ANDROID_ADB_SERVER_PORT=15187' in cmd;assert cmd[cmd.index('-datadir')+1]==str(root/'build/mobile-tools/avd/glob2-api35-x64.avd')
mounts=inspection['Mounts'];assert any(m['Source']=='/' and m['Destination']=='/host' and not m['RW'] for m in mounts);assert [m['Source'] for m in mounts if m['RW']]==[str(root/'build/mobile-tools')];assert inspection['HostConfig']['Devices']==[{'PathOnHost':'/dev/kvm','PathInContainer':'/dev/kvm','CgroupPermissions':'rwm'}]
server=1335126;proc=Path(f'/proc/{server}');server_args=(proc/'cmdline').read_bytes().split(b'\0');expected=[str(adb),'-P','15187','--one-device','glob2-mobile-no-usb','server','nodaemon'];assert [a.decode() for a in server_args if a]==expected;assert (proc/'cwd').resolve()==root
list_before=output([adb,'-P','15187','devices','-l']);devices=[l.split()[0] for l in list_before.splitlines()[1:] if l.strip()];assert devices==['emulator-5678'];assert output([adb,'-P','15187','-s','emulator-5678','shell','getprop','ro.boot.qemu.avd_name']).strip()=='glob2-api35-x64'
gate=json.loads((b/'android-evidence-gate-recommendation.json').read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();preserved={}
for field in ('unsigned_apk','developer_signed_release_apk'):
 p=Path(gate[field]['path']);assert sha(p)==gate[field]['sha256'];preserved[str(p)]=gate[field]['sha256']
assert (root/'build/mobile-tools/avd/glob2-api35-x64.avd/userdata-qemu.img').is_file();record={'ownership_verified':{'container_id':cid,'container_name':inspection['Name'],'command':cmd,'mounts':mounts,'devices':inspection['HostConfig']['Devices'],'adb_server_pid':server,'adb_server_command':expected,'adb_server_cwd':str(root),'isolated_devices_before':list_before},'operations':[],'preserved_apks_sha256':preserved,'no_host_acl_or_group_change':True,'shared_sdk_modified':False};(b/'android-runtime-cleanup.json').write_text(json.dumps(record,indent=2)+'\n')
with (b/'android/emulator-final-tail-1000.log').open('w') as log:run(['docker','logs','--tail','1000',cid],stdout=log,stderr=subprocess.STDOUT,check=True)
command=['docker','stop','--time','30',cid];print('Stopping only verified owned emulator container',cid,flush=True);done=run(command,capture_output=True,text=True,timeout=45);assert done.returncode==0,(done.stdout,done.stderr);record['operations'].append({'command':command,'exit_code':done.returncode,'stdout':done.stdout});assert run(['docker','inspect',cid],capture_output=True).returncode!=0
assert proc.exists() and [a.decode() for a in (proc/'cmdline').read_bytes().split(b'\0') if a]==expected
remaining=output([adb,'-P','15187','devices','-l']);assert all(l.split()[0]=='emulator-5678' for l in remaining.splitlines()[1:] if l.strip()),remaining
command=[str(adb),'-P','15187','kill-server'];print('Stopping only verified isolated ADB server15187',flush=True);done=run(command,capture_output=True,text=True,timeout=15);assert done.returncode==0,(done.stdout,done.stderr);record['operations'].append({'command':command,'exit_code':done.returncode,'stdout':done.stdout,'devices_before_kill':remaining})
for _ in range(100):
 if not proc.exists():break
 time.sleep(0.1)
assert not proc.exists();listeners=output(['ss','-ltnp','sport = :15187']);assert len(listeners.strip().splitlines())==1,listeners
for p,digest in preserved.items():assert sha(p)==digest
assert (root/'build/mobile-tools/avd/glob2-api35-x64.avd/userdata-qemu.img').is_file();record.update({'container_removed_after_graceful_stop':True,'adb_server_gone':True,'adb_listener_after':listeners,'avd_files_preserved':str(root/'build/mobile-tools/avd/glob2-api35-x64.avd'),'reproducibility_artifacts_preserved':str(b),'apks_rehashed_unchanged_after_cleanup':True,'status':'owned runtime stopped; artifacts preserved'});(b/'android-runtime-cleanup.json').write_text(json.dumps(record,indent=2)+'\n');print('PASS owned container and ADB stopped; AVD/APKs/all evidence preserved',flush=True)
