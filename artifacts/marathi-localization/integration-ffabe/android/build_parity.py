"""RUN ONLY after parent announces final golden commit + official Android build completes."""
from pathlib import Path
import json,hashlib,subprocess,shlex,sys,time,os
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-ffabe/android';out=root/'build/android/device/x86_64/24/client/release';parity=base/'parity';parity.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();git=lambda *a:subprocess.check_output(['git',*a],text=True).strip()
assert len(sys.argv)==2;expected=sys.argv[1];assert git('rev-parse','HEAD')==expected and not git('status','--porcelain')
proof=json.loads((base/'producer-provenance.json').read_text());assert proof['state']=='verified frozen shipping official build';assert set(git('diff','--name-only',proof['compiled_shipping_revision'],expected).splitlines())<={'test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt'};assert '#define SIM_REVISION 112' in (root/'src/game/SimRevision.h').read_text()
commands=json.loads((out/'compile_commands.json').read_text());main=next(c for c in commands if c['file']=='src/app/Glob2.cpp');compile=shlex.split(main['command']);assert compile[-1]=='src/app/Glob2.cpp';assert '-c' in compile and '-fPIC' in compile
linkline=next(l for l in reversed((base/'build.log').read_text().splitlines()) if '-o build/android/device/x86_64/24/client/release/lib/libmain.so' in l);link=shlex.split(linkline);assert link[0]==compile[0] and '-shared' in link
objects=[Path(p) for p in link if p.endswith('.o')];mainobj=Path(main['output']);assert mainobj in objects and all(p.is_file() for p in objects)
inputs={str(p):sha(p) for p in objects};libraryinputs={p:sha(p) for p in link if p.endswith(('.a','.so')) and Path(p).is_file()};saved={'expected_clean_source':expected,'compiled_engine_shipping_revision':proof['compiled_shipping_revision'],'purpose':'AndroidCPUheadlesssim112parity using exact official releaseengineobjects plus ignored CLIentry/existing native-test bridges. Not installedAPK/JVM.','official_producer_sha256':sha(base/'producer-provenance.json'),'compile_recipe_sha256':sha(out/'compile_commands.json'),'official_link_line_sha256':hashlib.sha256(linkline.encode()).hexdigest(),'object_sha256':inputs,'library_sha256':libraryinputs,'excluded_main_object':str(mainobj),'commands':[],'status':'preparing'}
def save():(parity/'build-provenance.json').write_text(json.dumps(saved,indent=2)+'\n')
def run(command,log):
 saved['commands'].append(command);save();start=time.monotonic()
 with (parity/log).open('wb') as f:
  process=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  while process.poll() is None:
   if time.monotonic()-start>600 or f.tell()>8*1024**2:
    os.killpg(process.pid,15);process.wait(timeout=10);raise RuntimeError('Owned fixture compiler/linker deadline/log cap exceeded')
   time.sleep(.2)
  assert process.returncode==0,log
 save()
extra=[]
for name,source in [('entry',base/'ParityMain.cpp'),('bridge',root/'mobile/NativeTestMain.cpp'),('global',root/'test/support/GlobalContainerSlot.cpp')]:
 dst=parity/(name+'.o');args=list(compile);args[args.index('-o')+1]=str(dst);args[-1]=str(source);run(args,name+'-compile.log');extra.append(str(dst));saved.setdefault('fixture_sources',{})[str(source)]=sha(source)
exe=parity/'android-match-verify';args=[a for a in link if a!=str(mainobj)];args[args.index('-o')+1]=str(exe);args[args.index('-shared')]='-pie';args+=['-Wl,--export-dynamic']+extra;run(args,'link.log')
strip=Path(compile[0]).with_name('llvm-strip');stripped=parity/'android-match-verify-stripped';run([str(strip),'--strip-debug','-o',str(stripped),str(exe)],'strip.log');stripped.chmod(0o755)
assert all(sha(Path(p))==h for p,h in inputs.items()) and all(sha(Path(p))==h for p,h in libraryinputs.items());assert git('rev-parse','HEAD')==expected and not git('status','--porcelain')
saved.update({'status':'verified ignored Android headless fixture built from official releaseobjects','binary':str(stripped),'binary_sha256':sha(stripped),'unstripped_sha256':sha(exe),'object_count':len(objects),'fixture_added_sources':3,'bridge_scope':'NativeTestMain interposes stdiofileIO/nullJNI/SDLpoll; production game/math/order/verification objects unchanged.','fixture_link_flags_delta':['-shared replaced by -pie','--export-dynamic for explicit existing bridge symbol interposition']});save();print('PASS ignored parity fixture built',sha(stripped))
