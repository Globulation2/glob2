from pathlib import Path
import os,json,subprocess,time,shlex,hashlib,shutil,struct
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native';build=Path('build/linux/client/debug/dev-dev_fast-true-linker-auto/test');env=dict(os.environ);env.update(json.loads((base/'build-env.json').read_text()))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
while True:
 request=json.loads((base/'build-request.json').read_text())
 if 'exit_code' in request:
  assert request['exit_code']==0,'Official harness build failed'
  break
 time.sleep(2)
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==request['revision'];assert not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
objects=['engine-support_TestMain.o','engine-support_Glob2Test.o','engine-support_GlobalContainerSlot.o','engine-support_EngineFixtures.o','engine-support_LegacyResourceTypes.o','engine-libgag_src_FontKerningTest.o','engine-src_app_SystemLanguageTest.o','engine-src_hud_touch_GameGUITouchHarness.o','engine-src_hud_GUIInteractionCoverageTest.o']
cmd=['python3','tools/dev_build.py',*[str(build/x) for x in objects],'-j4'];print('Building official focused registry objects',flush=True)
with (base/'focused-build.log').open('w') as f:r=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0,'Focused official object build failed'
log=(base/'official-build.log').read_text();linkline=next(l for l in log.splitlines() if l.startswith('g++ -o '));link=shlex.split(linkline);position=next(i for i,s in enumerate(link) if s.endswith('/program-tools_TranslationPreviewHarness.o'));focused=list(link);focused[focused.index('-o')+1]=str(base/'focused-tests');focused[position:position+1]=[str(build/x) for x in objects]
with (base/'focused-link.log').open('w') as f:r=subprocess.run(focused,env=env,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0,'Focused link failed'
compileline=next(l for l in log.splitlines() if 'program-tools_TranslationPreviewHarness.o -c ' in l);compileargs=shlex.split(compileline);compileargs[compileargs.index('-o')+1]=str(base/'ExtraPreviewHarness.o');compileargs[compileargs.index('tools/TranslationPreviewHarness.cpp')]=str(base/'ExtraPreviewHarness.cpp');print('Compiling ignored capture adapter against official production objects',flush=True)
with (base/'adapter-compile.log').open('w') as f:r=subprocess.run(compileargs,env=env,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0,'Capture adapter compile failed'
adapterlink=list(link);adapterlink[adapterlink.index('-o')+1]=str(base/'ExtraPreviewHarness-original');adapterlink[position]=str(base/'ExtraPreviewHarness.o')
with (base/'adapter-link.log').open('w') as f:r=subprocess.run(adapterlink,env=env,stdout=f,stderr=subprocess.STDOUT)
assert r.returncode==0,'Capture adapter link failed'
def elf(p):
 d=p.read_bytes();h=struct.unpack_from('<16sHHIQQQIHHHHHH',d);assert h[0][4:6]==b'\x02\x01';ss=[struct.unpack_from('<IIQQQQIIQQ',d,h[6]+i*h[11]) for i in range(h[12])];names=ss[h[13]];strings=d[names[4]:names[4]+names[5]];alloc={}
 for s in ss:
  if s[2]&2:
   name=strings[s[0]:].split(b'\0',1)[0].decode();alloc[name]={'address':s[3],'type':s[1],'flags':s[2],'size':s[5],'alignment':s[8],'sha256':sha_bytes(d[s[4]:s[4]+s[5]]) if s[1]!=8 else 'NOBITS'}
 return {'path':str(p),'sha256':sha_bytes(d),'bytes':len(d),'entry':h[4],'program_headers_sha256':sha_bytes(d[h[5]:h[5]+h[9]*h[10]]),'allocated_sections':alloc}
def sha_bytes(d):return hashlib.sha256(d).hexdigest()
proofs={}
for name,source,destination in [('official',build/'TranslationPreviewHarness',base/'TranslationPreviewHarness-stripped'),('adapter',base/'ExtraPreviewHarness-original',base/'ExtraPreviewHarness-stripped')]:
 shutil.copyfile(source,destination);destination.chmod(0o755);command=['strip','--strip-all',str(destination)];subprocess.run(command,check=True);a=elf(source);z=elf(destination);assert a['allocated_sections']==z['allocated_sections'] and a['program_headers_sha256']==z['program_headers_sha256'] and a['entry']==z['entry'];proofs[name]={'command':command,'original':a,'executed_copy':z,'proof':'All SHF_ALLOC contents/address/type/flags/size/alignment, program headers and entry point are identical. Debug/symbol/nonruntime section metadata only is stripped; actual executed hashes stay distinct.'}
write(base/'strip-provenance.json',proofs);write(base/'focused-and-adapter-build-commands.json',{'revision':request['revision'],'official_focused_object_command':cmd,'registered_objects':objects,'focused_link':focused,'adapter_compile':compileargs,'adapter_link':adapterlink,'production_harness_link_log_line':linkline,'base_harness_source_sha256':sha(root/'tools/TranslationPreviewHarness.cpp'),'adapter_source_sha256':sha(base/'ExtraPreviewHarness.cpp'),'focused_binary_sha256':sha(base/'focused-tests')});write(base/'tools-ready.json',{'revision':request['revision'],'official':str(base/'TranslationPreviewHarness-stripped'),'adapter':str(base/'ExtraPreviewHarness-stripped'),'focused':str(base/'focused-tests'),'state':'all builds/stripping verified'});print('Native integration tools ready',flush=True)
