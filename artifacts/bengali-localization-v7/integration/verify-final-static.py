"""Read-only final static verification. Never installs, generates, edits or publishes."""
import argparse,pathlib,subprocess,json,hashlib,os,time,datetime
P=argparse.ArgumentParser(__doc__);P.add_argument('--revision',required=True);P.add_argument('--kit',type=pathlib.Path,required=True);P.add_argument('--sdl-prefix',type=pathlib.Path,default=pathlib.Path('build/sdl3-ci/prefix'));P.add_argument('--phase',choices=['python','online','typecheck','all'],default='all');A=P.parse_args();root=pathlib.Path.cwd();kit=A.kit.resolve();assert kit.is_relative_to(root/'artifacts')
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
assert git('rev-parse','HEAD')==A.revision,'Wrong revision';assert not git('status','--porcelain=v1'),'Tracked source must be clean'
manifest=json.load(open(kit/'manifest.json'))
for name,expected in manifest['source_files'].items():assert sha(root/name)==expected,name
node=pathlib.Path('/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/node/24.19.0_64bit/bin');assert (node/'node').is_file();prefix=A.sdl_prefix.resolve();assert (prefix/'lib').is_dir();env=dict(os.environ,PATH=str(node)+os.pathsep+os.environ['PATH'],GLOB2_SDL3_PREFIX=str(prefix));stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');out=kit/'integration'/('static-'+A.phase+'-'+stamp);out.mkdir(parents=True,exist_ok=False)
checks=[]
if A.phase in ['python','all']:
 checks=[('pinned-script-fonts',['python3','data/fonts/fetch_script_fonts.py'],root),('native-font-coverage',['python3','test/test_font_coverage.py'],root),('native-strict',['python3','data/check_translations.py','--strict'],root),('native-translation-tests',['python3','test/test_translations.py'],root),('translation-program-tests',['python3','test/test_translation_program.py'],root),('cloud-draft-tests',['python3','test/test_cloud_draft.py'],root),('google-draft-tests',['python3','test/test_google_draft.py'],root),('bengali-draft',['python3','tools/localization/translation_program.py','check-draft','--output',str(kit),'--locale','bn'],root)]
if A.phase in ['online','all']:
 checks += [('online-contract',['npm','run','i18n:check'],root/'platform'),('online-units',['npm','test','--','packages/i18n/test','apps/web/test/i18n.test.tsx'],root/'platform')]
if A.phase in ['typecheck','all']:
 checks += [('platform-typecheck',['npm','run','typecheck'],root/'platform'),('scoped-format',['node_modules/.bin/prettier','--check','packages/i18n/src','packages/i18n/scripts','packages/i18n/test','packages/i18n/locales/bn.json','apps/web/src/App.tsx','apps/web/src/styles/layout.css','apps/web/e2e/i18n.spec.ts'],root/'platform')]
report={'source_revision':A.revision,'git_tree':git('rev-parse','HEAD^{tree}'),'source_manifest_sha256':manifest['source_sha256'],'node':subprocess.check_output([str(node/'node'),'--version'],text=True).strip(),'sdl_prefix':str(prefix),'sdl_library_sha256':{str(p):sha(p) for p in sorted((prefix/'lib').glob('*SDL3*.so*')) if p.is_file()},'note':'Actual checks only; this does not set any review gate or claim rendering/platform execution.','checks':[]}
for name,cmd,cwd in checks:
 assert git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1'),'Source changed before '+name
 start=time.monotonic();log=out/(name+'.log')
 with log.open('w') as f:r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=600)
 assert log.stat().st_size<40*1024*1024,'Unexpectedly large diagnostic log'
 report['checks'].append({'name':name,'command':cmd,'cwd':str(cwd.relative_to(root)),'exit_code':r.returncode,'seconds':time.monotonic()-start,'log':log.name,'sha256':sha(log)})
 report['source_stable_after_check']=git('rev-parse','HEAD')==A.revision and not git('status','--porcelain=v1');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 assert report['source_stable_after_check'],'Source changed during '+name
report['all_executed_checks_passed']=all(c['exit_code']==0 for c in report['checks']);(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'result':str(out/'result.json'),'passed':report['all_executed_checks_passed']}));raise SystemExit(not report['all_executed_checks_passed'])
