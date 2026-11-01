from pathlib import Path
import sys,os,json,time,signal,subprocess,shutil,hashlib,zipfile
sys.path.insert(0,'scons')
import shared_dependencies as shared,dev_store as store
root=Path.cwd();base=root/'artifacts/marathi-localization';record=json.loads((base/'android-recording-reuse-candidates.json').read_text());prefix=Path(record['expected_release_prefix']);identity=record['expected_shared_identity'];pid=1590244;mobile_pid=1590066
print('Waiting for exact current-release recording receipt',prefix,flush=True)
while not (prefix/'.shared-install.json').is_file():
 if not Path(f'/proc/{pid}').exists():raise RuntimeError('Own builder exited before publishing receipt; inspect original log')
 time.sleep(5)
assert shared.verify(prefix,identity)
manifest=json.loads((prefix/'recording-manifest.json').read_text());assert manifest['identity']==record['expected_recording_identity']
for name,digest in manifest['archives'].items():assert shared.digest(prefix/'lib'/name)==digest
store.hold(prefix.parent)
proof={'current_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'recording_prefix':str(prefix),'recording_manifest':manifest,'shared_receipt_sha256':shared.digest(prefix/'.shared-install.json'),'receipt_verification':'Current full shared identity and every installation file SHA verified; exact fresh release build; no debug receipt reused','rejected_candidates_record':'android-recording-reuse-candidates.json','commands':[]}
link=root/'build/android/device/x86_64/24/client/release/recording/prefix';link.parent.mkdir(parents=True,exist_ok=True)
assert not link.exists() or (link.is_symlink() and link.resolve()==prefix.resolve())
if not link.exists():link.symlink_to(prefix,target_is_directory=True)
proof['packager_license_path']={'link':str(link),'target':str(prefix),'reason':'Existing packager reads recording notices from local output path; immutable exact managedprefix is exposed without tracked infrastructure changes.'}
(base/'android-build-restart-provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
print('Exact release recording verified and exposed for packaging',flush=True)
proc=Path(f'/proc/{pid}')
if proc.exists():
 assert (proc/'cwd').resolve()==root
 command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode();assert '/usr/bin/scons target=android arch=x86_64 release=1' in command
 os.kill(pid,signal.SIGINT);print('Sent SIGINT only to own application SCons',pid,flush=True)
 while proc.exists():time.sleep(1)
while Path(f'/proc/{mobile_pid}').exists():time.sleep(1)
shutil.copyfile(base/'android-build.log',base/'android-build-initial-recording.log')
command=['python3','mobile/android.py','build','--arch','x86_64','--release','--jobs','6'];proof['commands'].append(command);print('Starting release Android application with6jobs',flush=True)
with (base/'android-build-final.log').open('w') as log:status=subprocess.call(command,stdout=log,stderr=subprocess.STDOUT)
proof['application_build_exit_code']=status;(base/'android-build-restart-provenance.json').write_text(json.dumps(proof,indent=2)+'\n');assert status==0,'Android application build failed; inspect android-build-final.log'
apk=root/'build/android/device/x86_64/24/client/release/android-project/app/build/outputs/apk/release/app-release-unsigned.apk';assert apk.is_file();license_hashes={str(p.relative_to(prefix/'share/licenses/recording')):shared.digest(p) for p in (prefix/'share/licenses/recording').rglob('*') if p.is_file()}
with zipfile.ZipFile(apk) as archive:
 for name,digest in license_hashes.items():assert hashlib.sha256(archive.read('assets/glob2-bundle/licenses/recording/'+name)).hexdigest()==digest,name
proof['apk']={'path':str(apk),'sha256':shared.digest(apk),'recording_license_files_verified_sha256':license_hashes};(base/'android-build-restart-provenance.json').write_text(json.dumps(proof,indent=2)+'\n');print('ReleaseAPKcomplete; exact recording notices verified',apk,flush=True)
