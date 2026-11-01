from pathlib import Path
import json,hashlib,subprocess,os,gzip,time,re
ROOT=Path(__file__).resolve().parents[4];B=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
key='[AI for all computer colonies]';target='सर्व संगणकनियंत्रित वसाहतींसाठी एआय'
current=(ROOT/'data/texts.mr.txt').read_text(encoding='utf-8').splitlines();assert current[current.index(key)+1]==target
native_sha=sha(ROOT/'data/texts.mr.txt');fontpaths=list((ROOT/'data/fonts').rglob('*.ttf'))
paths=['tools/TranslationPreviewHarness.cpp','libgag/src/TrueTypeFont.cpp','libgag/src/BidiText.cpp','libgag/src/ui/TextLayout.cpp','libgag/src/DrawableSurfaceCompound.cpp','libgag/include/TrueTypeFont.h','libgag/include/ui/TextLayout.h']+[str(p.relative_to(ROOT)) for p in fontpaths]
proof={p:sha(ROOT/p) for p in paths}
unchanged=[p for p,v in proof.items() if hashlib.sha256(subprocess.check_output(['git','show','e4811dc:'+p])).hexdigest()==v]
assert set(proof)-set(unchanged)=={'libgag/src/ui/TextLayout.cpp','libgag/include/ui/TextLayout.h'}
oldcpp=subprocess.check_output(['git','show','e4811dc:libgag/src/ui/TextLayout.cpp'],text=True);newcpp=(ROOT/'libgag/src/ui/TextLayout.cpp').read_text()
norm=lambda text:re.sub(r'\s+',' ',text.replace('shapingClusterEnds','checkedClusterEnds').replace('} // namespace\n','')).strip()
assert norm(oldcpp)==norm(newcpp),'Text wrap behavior changed beyond linkage/name-only refactor'
layout_equivalence={'rule':'Entire TextLayout.cpp exact token/comment text after whitespace normalization, checkedClusterEnds->shapingClusterEnds identifier equivalence and anonymous namespace closing-boundary movement; function bodies/branches/call sites otherwise identical. Header merely declares newly exported helper.','old_cpp_sha256':hashlib.sha256(oldcpp.encode()).hexdigest(),'new_cpp_sha256':proof['libgag/src/ui/TextLayout.cpp'],'new_header_sha256':proof['libgag/include/ui/TextLayout.h'],'limits':'Old executable does not verify newly exported symbol linking or new text-input consumers.'}
inputs=B/'inputs';pilot=inputs/'previews/mr';pilot.mkdir(parents=True,exist_ok=False)
meta={'locale':'mr','source_snapshot':'Working-tree merge of origin/master1801; exact selected input/file hashes below, not a newly built executable.','base_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'workspace_dirty':bool(subprocess.check_output(['git','status','--porcelain'],text=True).strip()),'actual_native_catalog_sha256':native_sha,'actual_target':target,'source_key':key,'source':'AI for all computer colonies','source_english_sha256':sha(ROOT/'data/texts.en.txt'),'current_font_wrap_code_and_assets_sha256':proof,'exact_unchanged_files':unchanged,'text_layout_export_refactor_equivalence':layout_equivalence,'limits':'Existing actual official e481 font-matrix executable; selected new1 text rendered freshly. Does not execute newly changed CustomGameScreen widget.'}
(pilot/'native.json').write_text(json.dumps({key:target},ensure_ascii=False)+'\n',encoding='utf-8');(pilot/'online.json').write_text('{}\n');(pilot/'metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');(inputs/'manifest.json').write_text(json.dumps({'revision':'working-tree-merge-1801-selected-source-hash-'+meta['source_english_sha256'],'scope':meta['source_snapshot']})+'\n')
binary=ROOT/'artifacts/marathi-localization/integration-ffabe/native/official-capture-stripped';identity=sha(binary)
env=dict(os.environ,SDL_VIDEO_DRIVER='x11',SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',XDG_SESSION_TYPE='x11',LIBGL_ALWAYS_SOFTWARE='1');env['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
for n in ['DISPLAY','WAYLAND_DISPLAY']:env.pop(n,None)
record={'actual_executable':str(binary),'actual_executable_sha256':identity,'actual_executable_code_producer':'e4811dcde89d6393918c36a99d34242ef3be8178','input':meta,'jobs':[],'per_attempt_deadline_seconds':180,'campaign_byte_cap':128*1024**2,'minimum_free_bytes':2*1024**3}
import shutil
for w,h in [(1280,900),(390,844),(304,844)]:
 assert sha(ROOT/'data/texts.mr.txt')==native_sha and all(sha(ROOT/p)==v for p,v in proof.items()) and sha(binary)==identity
 dest=B/f'{w}x{h}';dest.mkdir(exist_ok=False);profile=B/'profiles'/f'{w}x{h}'
 e=dict(env,GLOB2_USER_DATA_DIR=str(profile),XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'))
 cmd=['xvfb-run','-a','-s','-screen 0 1600x1200x24 -noreset','python3','test/xvfb_session.py',str(binary),'matrix',str(ROOT),str(pilot),str(dest),str(w),str(h)]
 t=time.monotonic();p=subprocess.run(cmd,cwd=ROOT,env=e,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
 with gzip.open(dest/'run.log.gz','wb') as f:f.write(p.stdout)
 assert p.returncode==0,p.stdout.decode(errors='replace')
 r=json.loads((dest/'font-matrix.json').read_text());assert len(r['samples'])==6 and all(s['has_glyphs'] and all(l['drawn_fits_width'] and l['drawn_fits_height'] for l in s['lines']) for s in r['samples'])
 record['jobs'].append({'viewport':[w,h],'actual_line_width':w-24,'exit_code':p.returncode,'command':cmd,'seconds':time.monotonic()-t,'report_sha256':sha(dest/'font-matrix.json'),'samples':6,'actual_lines':sum(len(s['lines']) for s in r['samples']),'raw_diagnostics':[{'size':s['size'],'scale':s['scale'],**l} for s in r['samples'] for l in s['lines'] if not l['fits_width'] or not l['fits_height']]})
 assert sum(p.stat().st_size for p in B.rglob('*') if p.is_file())<record['campaign_byte_cap'];assert shutil.disk_usage(B).free>record['minimum_free_bytes']
 (B/'probe-results.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
assert sha(ROOT/'data/texts.mr.txt')==native_sha and all(sha(ROOT/p)==v for p,v in proof.items())
record.update(complete=True,actual_new_samples=18,missing_glyphs=0,drawn_bounds_failures=0,scope='12 actual samples at desktop/compact widths plus6 static280px label-line wrap probes. Not a new-widget runtime/layout claim.')
(B/'probe-results.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Fresh18 font/wrap samples complete.',flush=True)
