from pathlib import Path
import os,sys,json,time,subprocess,hashlib,zipfile
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-refresh/android';output=root/'build/android/device/x86_64/24/client/release'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
def write(name,data):(base/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def objects():return {str(p.relative_to(output)):sha(p) for p in sorted((output/'obj').rglob('*.o'))}
assert len(sys.argv)==2,'Pass exact clean integrated revision'
revision=sys.argv[1];assert git('rev-parse','HEAD')==revision and not git('status','--porcelain')
sys.path[:0]=[str(root/'scons'),str(root/'mobile')]
from build_layout import build_identity
from dev_build import dependency_identity
from mobile_toolchain import discover
from dev_store import command_path,home
from recording_dependencies import build
from shared_dependencies import fingerprint,verify
options={'target':'android','arch':'x86_64','environment':'device','release':1,'china':0}
identity=dependency_identity(build_identity(options));tc=discover(identity,options)
env=dict(os.environ)
for n in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','SDKROOT','MACOSX_DEPLOYMENT_TARGET'):env.pop(n,None)
arguments={'sdk_identity':tc['fingerprint'],'cc':command_path(tc['cc']),'cxx':command_path(tc['cxx']),'ar':command_path(tc['ar']),'ranlib':command_path(Path(tc['ar']).with_name('llvm-ranlib')),'target':'android','arch':'x86_64','cflags':tc['cflags'],'ldflags':tc['ldflags'],'environment':env}
f=json.loads(json.dumps(fingerprint(build,arguments),sort_keys=True,default=str));key='build-'+hashlib.sha256(json.dumps(f,sort_keys=True,default=str).encode()).hexdigest()[:24];prefix=home()/'dependencies'/key/'prefix'
record={'initial_native_producer_revision':'6e376b299948b57e3d4e6f22673b4b0acc0730c4','previous_verified_revision':'ba890db53e67617fb3cb1033c157ca578781196e','previous_validated_archive':'validated-android-ba890/archive-manifest.json','final_revision':revision,'state':'official integrated Android build preparing','final_source_tree_status_before':git('status','--porcelain'),'final_source_git_trees':git('ls-tree','HEAD','src','libgag','libusl','third_party','scons','mobile','data','SConstruct'),'changes_since_previous_verified':git('diff','--name-only','ba890db53e67617fb3cb1033c157ca578781196e',revision).splitlines(),'recording_prefix':str(prefix),'recording_identity':f,'toolchain_fingerprint':tc['fingerprint'],'recording_exact_preexisting':verify(prefix,f),'notes':['Old ba890 runtime evidence remains separate and is not relabeled.','SCons validates the actual integrated source, dependencies and headers before object reuse. Hash identity alone does not establish compiler invocation.','Task-local packaging symlink points to the exact current official managed recording prefix; no SDKidentity or receipts rewritten.']}
write('producer-provenance.json',record)
before=objects();write('object-inputs-before.json',before)
commands=[['python3','mobile/dependencies.py','--target','android','--arch','x86_64','--release','--jobs','6'],['python3','mobile/android.py','build','--arch','x86_64','--release','--jobs','6']]
record['commands']=commands
for number,command in enumerate(commands):
 if number==1:
  link=output/'recording/prefix';assert link.is_symlink(),'Refuse to replace an unexpected local recording directory'
  record['packaging_symlink_before']=str(link.readlink());link.unlink();link.symlink_to(prefix,target_is_directory=True);record['packaging_symlink_after']=str(link.readlink())
 record['state']='official dependency validation running' if number==0 else 'official final integrated application build running';write('producer-provenance.json',record)
 with (base/('dependencies.log' if number==0 else 'build.log')).open('w') as log:status=subprocess.call(command,stdout=log,stderr=subprocess.STDOUT)
 record['dependency_exit_code' if number==0 else 'application_build_exit_code']=status;write('producer-provenance.json',record)
 assert status==0,command
assert git('rev-parse','HEAD')==revision and not git('status','--porcelain'),'Source changed during integrated build'
assert verify(prefix,f),'Official current recording installation must verify exact identity and contents'
after=objects();write('object-inputs-after.json',after)
record['objects']={'before_count':len(before),'after_count':len(after),'unchanged_object_bytes':sum(after.get(n)==d for n,d in before.items()),'changed_or_new':[n for n,d in after.items() if before.get(n)!=d],'removed':[n for n in before if n not in after],'before_manifest_sha256':sha(base/'object-inputs-before.json'),'after_manifest_sha256':sha(base/'object-inputs-after.json')}
apk=output/'android-project/app/build/outputs/apk/release/app-release-unsigned.apk';licenses={str(p.relative_to(prefix/'share/licenses/recording')):sha(p) for p in (prefix/'share/licenses/recording').rglob('*') if p.is_file()}
with zipfile.ZipFile(apk) as archive:
 for name,digest in licenses.items():assert hashlib.sha256(archive.read('assets/glob2-bundle/licenses/recording/'+name)).hexdigest()==digest,name
record['apk']={'path':str(apk),'sha256':sha(apk),'recording_license_files_verified_sha256':licenses};record['staged_marathi_xml_sha256']=sha(output/'android-project/app/src/main/res/values-mr/strings.xml');assert record['staged_marathi_xml_sha256']==sha(root/'mobile/android/app/src/main/res/values-mr/strings.xml');record['final_source_tree_status_after']=git('status','--porcelain');record['state']='verified final clean official build';write('producer-provenance.json',record)
print('Final clean integrated Android producer verified',revision,flush=True)
