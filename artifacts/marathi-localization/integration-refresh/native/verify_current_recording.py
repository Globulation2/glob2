from pathlib import Path
import os,sys,json,hashlib,subprocess,platform
root=Path.cwd();sys.path.insert(0,str(root/'scons'))
import shared_dependencies as shared,recording_dependencies as recording,dev_store as store
base=root/'artifacts/marathi-localization/integration-refresh/native';prefix=Path('/home/bradley/.local/share/glob2/development/dependencies/build-5bb1997bf62870279ace1d52/prefix');env=dict(os.environ);env['PKG_CONFIG_PATH']=str(root/'artifacts/marathi-localization/native-deps/lib/pkgconfig')+':'+env.get('PKG_CONFIG_PATH','');env.pop('PKG_CONFIG_LIBDIR',None)
versions={k:v for k,v in json.loads(recording.LOCK.read_text()).items() if k in recording.codec_sources('h264')}
probe=subprocess.run(['pkg-config','--modversion','libva','libva-drm'],env=env,capture_output=True,text=True);va=probe.stdout.strip() if probe.returncode==0 else None
expected=dict(configuration=3,video_codec='h264',opus=None,versions=versions,target='linux',arch='x86_64',compiler=subprocess.check_output(['gcc','--version'],env=env,text=True),cc='gcc',ar='ar',flags=[[],[]],vaapi_api=va,assembler=subprocess.check_output(['nasm','--version'],env=env,text=True).strip(),sdk=None,environment={k:env.get(k,'') for k in ('CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','MACOSX_DEPLOYMENT_TARGET','DEVELOPER_DIR')},recipe=hashlib.sha256(Path(recording.__file__).read_bytes()).hexdigest())
actual=json.loads((prefix/'recording-manifest.json').read_text());assert actual['identity']==expected,{k:(actual['identity'].get(k),v) for k,v in expected.items() if actual['identity'].get(k)!=v}
receipt=json.loads((prefix/'.shared-install.json').read_text());args=dict(cc='gcc',cxx='g++',ar='ar',ranlib='ranlib',target='linux',video_codec='h264',opus_configuration=None,environment=env,jobs=4);current_shared=shared.fingerprint(recording.build,args)
with store.Lease(prefix.parent):
 assert shared.verify(prefix,receipt['identity']),'Original shared receipt/content invalid'
 before=shared.contents(prefix);receipt_sha=shared.digest(prefix/'.shared-install.json')
 validated=recording.build(prefix,base/'recording-validation-work',**args)
 after=shared.contents(prefix);assert before==after and shared.digest(prefix/'.shared-install.json')==receipt_sha
 differences={k:{'stored':receipt['identity'].get(k),'current':v} for k,v in current_shared.items() if receipt['identity'].get(k)!=v}
 assert set(differences)<= {'environment'},differences
 record={'prefix':str(prefix),'official_current_builder_accepted_without_writes':True,'configuration':3,'recording_manifest_sha256':shared.digest(prefix/'recording-manifest.json'),'shared_receipt_sha256':receipt_sha,'original_shared_identity':receipt['identity'],'current_raw_builder_identity':expected,'current_shared_fingerprint':current_shared,'shared_fingerprint_differences':differences,'file_count':len(before),'verified_files_sha256':before,'disposition':'Selected as an explicit current-recipe prefix. Full original shared receipt and contents verified, current official builder accepts the exact raw identity and archives, and every file remained unchanged. Different shared PKG_CONFIG_PATH fingerprint is explicitly retained; no cache-hit identity or old configuration1 relabeling is claimed.'}
 (base/'recording-prefix-validation.json').write_text(json.dumps(record,indent=2)+'\n');(base/'build-env.json').write_text(json.dumps({'GLOB2_SDL3_PREFIX':str(root/'artifacts/marathi-localization/native-deps'),'GLOB2_RECORDING_PREFIX':str(prefix)},indent=2)+'\n');print('Current official recording builder validated unchanged explicit prefix',prefix,flush=True)
