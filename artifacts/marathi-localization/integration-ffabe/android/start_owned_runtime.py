from pathlib import Path
import json,subprocess,socket,time,hashlib
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-ffabe/android';name='glob2-marathi-emulator'
old=root/'artifacts/marathi-localization/android/emulator-container-command.json';args=json.loads(old.read_text())['argv']
assert args[:5]==['docker','run','--rm','--name',name]
assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0,'Refuse to replace an existing container'
for port in (15187,5678,5679):
 with socket.socket() as s:assert s.connect_ex(('127.0.0.1',port))!=0,f'Port {port} already occupied'
adb_cmd=['python3','mobile/emulator.py','adb-server','--arch','x86_64','--port','5678','--adb-port','15187']
adb_log=(base/'adb-server.log').open('w');adb=subprocess.Popen(adb_cmd,stdout=adb_log,stderr=subprocess.STDOUT,start_new_session=True)
time.sleep(2);assert adb.poll() is None,'Owned adb-server did not stay running'
emu_log=(base/'emulator.log').open('w');emu=subprocess.Popen(args,stdout=emu_log,stderr=subprocess.STDOUT,start_new_session=True)
time.sleep(3);assert emu.poll() is None,'Owned emulator container did not stay running'
inspect=json.loads(subprocess.check_output(['docker','inspect',name],text=True))[0];assert inspect['Config']['Image']=='glob2-shared-deps-final:latest'
record={'argv':args,'saved_original_command_sha256':hashlib.sha256(old.read_bytes()).hexdigest(),'adb_launcher_argv':adb_cmd,'adb_launcher_pid':adb.pid,'container_launcher_pid':emu.pid,'container_id':inspect['Id'],'name':name,'serial':'emulator-5678','adb_port':15187,'console_port':5678,'host_permissions_changed':False,'avd_preserved':True,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'docker_inspect':inspect}
(base/'owned-runtime.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({k:record[k] for k in ('container_id','adb_launcher_pid','container_launcher_pid','serial','source_revision')},indent=2))
