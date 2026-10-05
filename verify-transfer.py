from pathlib import Path
import hashlib,json,os,shutil,subprocess,time
root=Path.cwd(); area=root/'artifacts/webp-ci-transfer'; source=root/'artifacts/native-export-99958'; audit=source.with_suffix('.json')
print('Waiting for completed verified asset export',flush=True)
for _ in range(1800):
 if audit.is_file() and source.is_dir():break
 time.sleep(2)
else:raise RuntimeError('Asset export did not finish within one hour')
base=area/'input'; dst=base/'build/linux/client/release/runtime-assets'
if not dst.exists():shutil.copytree(source,dst,copy_function=os.link)
shutil.copy2(audit,dst.with_suffix('.json'));(base/'artifacts/ci-native').mkdir(parents=True,exist_ok=True)
for kind in ('linux','web'):
 subprocess.run(['bash',str(area/(kind+'-package.sh'))],cwd=base,check=True)
 packed=base/'artifacts/ci-native'/('linux-test-programs.tar.gz' if kind=='linux' else 'web-native.tar.gz'); restored=area/(kind+'-restored');restored.mkdir(exist_ok=True)
 subprocess.run(['tar','-xzf',str(packed),'-C',str(restored)],check=True)
 restored_assets=restored/'build/linux/client/release/runtime-assets'; count=0
 for p in source.rglob('*'):
  if p.is_file():
   q=restored_assets/p.relative_to(source)
   assert q.read_bytes()==p.read_bytes(),str(p);count+=1
 assert restored_assets.with_suffix('.json').read_bytes()==audit.read_bytes()
 print(kind,'archive restores',count,'byte-identical asset files',flush=True)
 if kind=='linux':
  env=os.environ.copy();env['GLOB2_ASSET_DIR']=str(restored_assets);env['GLOB2_TEST_SOURCE_ROOT']=str(root);env['LD_LIBRARY_PATH']=str(restored/'build/sdl3-ci/prefix/lib')
  build=restored/'build/linux/client/release'
  command=['python3','test/run_tests.py','--binary','engine','--build-dir',str(build),'--verbose','--artifacts','artifacts/webp-ci-final-render']
  for pattern in ('UIIcons/*','UnitHighResolutionCache/*','UnitTeamShader/*','UnitTeamColorCache/*','RuntimePack/*','GameDiagnostics/*','*resource sprite batch retains pixels*','*unit batches preserve overlays*'):command+=['--filter',pattern]
  with (area/'render.log').open('w') as f:subprocess.run(command,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
  with (area/'cli.log').open('w') as f:subprocess.run(['python3','test/test_cli_smoke.py','--binary',str(build/'src/glob2'),'--artifacts','artifacts/webp-ci-final-cli','--junit','artifacts/webp-ci-final-cli.xml'],cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
print('TRANSFER AND NATIVE VERIFICATION PASSED',flush=True)
