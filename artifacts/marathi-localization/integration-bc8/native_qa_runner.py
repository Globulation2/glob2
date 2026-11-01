#!/usr/bin/env python3
"""Ignored, isolated production harness evidence; no production catalog writes."""
import argparse,hashlib,importlib.util,json,os,pathlib,shutil,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[3]; KIT=ROOT/'artifacts/marathi-localization/integration-bc8'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def prepare():
 spec=importlib.util.spec_from_file_location('p',ROOT/'tools/localization/translation_program.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
 revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True);assert revision=='64aeda1300e3fcb81b074aef1a9a685498edcf63' and not status
 _,_,_,source_files,source_hash=p.sources();assert source_hash=='b7ed54ddecb1840204b30775c4f8cc69c34cc23d0f11695ad9ece2593bd451bb'
 paths={'native':ROOT/'data/texts.mr.txt','online':ROOT/'platform/packages/i18n/locales/mr.json'};n,errors=p.CHECKER.read_table(paths['native']);assert not errors and len(n)==3411
 assert sha(paths['native'])=='540819f418f880607fad22780bd25692be84c33794fba08cf758ae0acb80d4eb'
 assert sha(paths['online'])=='0a35ecc3e76fd00b68db89ca5dd18967304524eb7c3c0b8cc81c37b70e5694a4'
 shutil.copyfile(KIT/'kit/manifest.json',KIT/'manifest.json')
 preview=KIT/'previews/mr';preview.mkdir(parents=True,exist_ok=True);write(preview/'native.json',n);shutil.copyfile(paths['online'],preview/'online.json')
 meta={'locale':'mr','source_revision':revision,'source_sha256':source_hash,'source_files':source_files,'original_catalog_sha256':{k:sha(v) for k,v in paths.items()},'preview_catalog_sha256':{k:sha(preview/(k+'.json')) for k in paths},'catalog_status':{k:'Complete independent semantic, editorial and adversarial reviews, with actual16-key final rereads and exact unchanged-old-value carry proofs' for k in paths},'limits':'Full canonical-value draft overlay in desktop production harness; registration/package startup is verified separately.'};write(preview/'metadata.json',meta)
 return preview

def diagnostics(folder):
 missing=[];overflows=[];samples=lines=0;identities=[];captures=[];controls=[]
 for path in sorted(folder.glob('**/*.json')):
  if path.name in ('runner.json','diagnostics.json'):continue
  x=json.loads(path.read_text())
  if not isinstance(x,dict):continue
  if 'evidence_identity' in x:identities.append({'report':str(path.relative_to(folder)),'identity':x['evidence_identity']})
  for sample in x.get('samples',[]):
   samples+=1
   if not sample['has_glyphs']:missing.append({k:sample[k] for k in ('surface','key','branch','size','scale')})
   for line in sample['lines']:
    lines+=1
    bad=[k for k in ('fits_width','fits_height','drawn_fits_width','drawn_fits_height') if not line[k]]
    if bad:overflows.append({'report':str(path.relative_to(folder)),**{k:sample[k] for k in ('surface','key','branch','size','scale')},'failed':bad,'line':line})
  if 'controls' in x:controls.append({'report':str(path.relative_to(folder)),'viewport':[x['width'],x['height']],'controls':x['controls'],'limits':'Bounds include scrollable content; this is an inventory, not an automatic text-fit verdict.'})
  captures.extend({'report':str(path.relative_to(folder)),**c} for c in x.get('captures',[]))
 result={'matrix_samples':samples,'matrix_lines':lines,'missing_glyphs':missing,'matrix_bounds_failures':overflows,'producer_identities':identities,'captures':captures,'screen_controls':controls,'limits':['Structured raster/glyph and logical bounds diagnostics cover every matrix sample and line.','Control geometry is reported without treating intentionally scrollable controls as clipping.','Representative visual inspections are recorded separately; no assertion that every image was manually inspected.','Desktop production harness with compact phone presentation is not an Android/iOS runtime claim.']};write(folder/'diagnostics.json',result);return result

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--binary',type=pathlib.Path);ap.add_argument('--output',type=pathlib.Path,default=KIT/'native-render-clean64');ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--resume',action='store_true');args=ap.parse_args();preview=prepare()
 if args.prepare_only:print(preview);return
 if not args.binary:ap.error('--binary required for captures')
 binary=args.binary.resolve();assert binary.is_file();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
 assert args.resume or not (out/'runner.json').exists(),'Use a new output directory or explicit --resume'
 env=os.environ.copy();env.update(SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',SDL_VIDEO_DRIVER='x11',XDG_SESSION_TYPE='x11',XDG_DATA_HOME=str(out/'profiles/data'),XDG_CONFIG_HOME=str(out/'profiles/config'),XDG_CACHE_HOME=str(out/'profiles/cache'))
 env['GLOB2_USER_DATA_DIR']=str(out/'profiles/user')
 env['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
 readfd,writefd=os.pipe()
 xvfb=subprocess.Popen(['Xvfb','-displayfd',str(writefd),'-screen','0','1600x1200x24','-nolisten','tcp'],pass_fds=[writefd],stdout=subprocess.DEVNULL,stderr=open(out/'xvfb.log','w'));os.close(writefd)
 try:
  with os.fdopen(readfd) as stream:display=stream.readline().strip()
  assert display,'Xvfb failed to allocate display';env['DISPLAY']=':'+display
  lessons=[json.loads((ROOT/e['definition']).read_text())['id'] for e in json.loads((ROOT/'scripts/tutorials/catalog.json').read_text())]
  plan=[]
  for w,h in [(1280,900),(390,844)]:
   for mode,kind in [('matrix',None)]+[('screen',s) for s in ('main','settings','settings-audio','lobby','lobby-players')]+[('game','gameplay')]+[('game','tutorial:'+s) for s in lessons]:plan.append((w,h,mode,kind))
  record={'display_environment':{'SDL_VIDEO_DRIVER':'x11','SDL_VIDEODRIVER':'x11','XDG_SESSION_TYPE':'x11','ownership':'Runner-owned Xvfb, replacing inherited desktop DISPLAY'},'binary':str(binary),'binary_sha256':sha(binary),'git_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'git_status':subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True),'preview_metadata':json.loads((preview/'metadata.json').read_text()),'jobs':[],'validation_deadlines_seconds':{'matrix':3600,'screen_and_game':300},'deadline_interpretation':'Bounded validation duration; not a performance guarantee or measurement.','maximum_evidence_bytes':2*1024**3,'minimum_free_bytes':2*1024**3};write(out/'runner.json',record) if not args.resume else None
  if args.resume:
   previous=json.loads((out/'runner.json').read_text())
   for field in ('binary_sha256','git_revision','git_status','preview_metadata'):assert previous[field]==record[field],('Resume input changed',field)
   record=previous
  spec=importlib.util.spec_from_file_location('identity',ROOT/'test/build_provenance.py');identity=importlib.util.module_from_spec(spec);spec.loader.exec_module(identity);current_source=identity.source_identity(ROOT)
  def completed(w,h,mode,kind):
   matches=[j for j in record['jobs'] if j['viewport']==[w,h] and j['mode']==mode and j['kind']==kind and j['returncode']==0]
   if not matches:return False
   dest=out/matches[-1]['output'];report=dest/('font-matrix.json' if mode=='matrix' else kind.replace(':','-')+'.json');assert report.is_file(),('Missing completed report',report)
   report=json.loads(report.read_text());e=report['evidence_identity'];assert e['executable_sha256']==record['binary_sha256'];assert not identity.build_issues(e['producer'],current_source);assert e['catalog_sha256']==record['preview_metadata']['preview_catalog_sha256']
   names={line['png'] for sample in report.get('samples',[]) for line in sample['lines']}
   names.update(c['name']+'.png' for c in report.get('captures',[]))
   if mode=='screen':names.add(kind+'.png')
   assert all((dest/name).is_file() for name in names),('Missing completed image',dest)
   return True
  for w,h,mode,kind in plan:
   if args.resume and completed(w,h,mode,kind):print('Verified completed resume',w,h,mode,kind,flush=True);continue
   used=sum(p.stat().st_size for folder in (out,KIT/'native-extra-clean64') if folder.exists() for p in folder.rglob('*') if p.is_file());assert used<record['maximum_evidence_bytes'],'Evidence byte budget exhausted';assert shutil.disk_usage(out).free>record['minimum_free_bytes'],'Disk reserve exhausted';assert sha(binary)==record['binary_sha256'],'Producer binary changed midrun'
   dest=out/f'{w}x{h}'/(kind.replace(':','-') if kind else mode);dest.mkdir(parents=True,exist_ok=True)
   if (dest/'capture.log').exists():shutil.copyfile(dest/'capture.log',dest/('capture-prior-'+str(len(record['jobs']))+'.log'))
   cmd=[str(binary),mode,str(ROOT),str(preview),str(dest),str(w),str(h)]+([kind] if kind else [])
   jobenv=dict(env);jobenv['GLOB2_MOBILE_UI']='touch-auto' if w==390 else '0';start=time.monotonic()
   with (dest/'capture.log').open('w') as log:
    child=subprocess.Popen(cmd,cwd=ROOT,env=jobenv,stdout=log,stderr=subprocess.STDOUT)
    try:
     while child.poll() is None:
      if time.monotonic()-start>(3600 if mode=='matrix' else 300):raise RuntimeError('Per-attempt time budget exhausted')
      if sum(p.stat().st_size for folder in (out,KIT/'native-extra-clean64') if folder.exists() for p in folder.rglob('*') if p.is_file())>record['maximum_evidence_bytes']:raise RuntimeError('Evidence byte budget exhausted during capture')
      if shutil.disk_usage(out).free<record['minimum_free_bytes']:raise RuntimeError('Disk reserve exhausted during capture')
      time.sleep(1)
    except BaseException as failure:
     child.terminate();child.wait(timeout=10);record['jobs'].append({'viewport':[w,h],'mode':mode,'kind':kind,'returncode':child.returncode,'seconds':round(time.monotonic()-start,2),'output':str(dest.relative_to(out)),'failure':str(failure)});write(out/'runner.json',record);raise
    r=child
   record['jobs'].append({'viewport':[w,h],'mode':mode,'kind':kind,'returncode':r.returncode,'seconds':round(time.monotonic()-start,2),'output':str(dest.relative_to(out))});write(out/'runner.json',record);print(record['jobs'][-1],flush=True)
   if r.returncode:raise RuntimeError('Capture failed; inspect '+str(dest/'capture.log'))
  d=diagnostics(out);print('Matrix samples',d['matrix_samples'],'missing glyphs',len(d['missing_glyphs']),'bounds failures',len(d['matrix_bounds_failures']),flush=True)
 finally:xvfb.terminate();xvfb.wait(timeout=10)
if __name__=='__main__':main()
