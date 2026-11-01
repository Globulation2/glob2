"""Prepare exact canonical native/UI and new111-only font samples; no rendering/build."""
from pathlib import Path
import hashlib,importlib.util,json,subprocess
ROOT=Path(__file__).resolve().parents[4];BASE=Path(__file__).resolve().parent
B=BASE.parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
spec=importlib.util.spec_from_file_location('program',ROOT/'tools/localization/translation_program.py');program=importlib.util.module_from_spec(spec);spec.loader.exec_module(program)
_,_,_,source_files,source_hash=program.sources();assert source_hash=='822cf8613451177b0e2fa803451eaf2308168f21f4d04b7af2aa36cfe64bc3fc'
np=ROOT/'data/texts.mr.txt';op=ROOT/'platform/packages/i18n/locales/mr.json';n,errors=program.CHECKER.read_table(np);assert not errors and len(n)==3411
o=json.loads(op.read_text(encoding='utf-8'));selected=json.loads((B/'final-targets111.json').read_text(encoding='utf-8'));assert len(selected)==111
assert all(o[k]==v for k,v in selected.items())
assert sha(np)=='540819f418f880607fad22780bd25692be84c33794fba08cf758ae0acb80d4eb' and sha(op)=='341b1d718230af75bc6593a44b2d4ae6bc783ed3430e280d4f1899cb976780d5'
rev=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
inputs=BASE/'preview-inputs';write(inputs/'manifest.json',{'revision':rev,'source_sha256':source_hash,'origin':'Actual clean frozen source revision; selection inputs copied verbatim from canonical catalogs.'})
for name,native,online in [('full',n,o),('new111',{},selected)]:
 p=inputs/'previews'/name
 write(p/'native.json',native);write(p/'online.json',online)
 write(p/'metadata.json',{'locale':'mr','source_revision':rev,'source_sha256':source_hash,'source_files':source_files,'canonical_catalog_sha256':{'native':sha(np),'online':sha(op)},'selected_native_keys':len(native),'selected_online_keys':len(online),'selection':'All3411native/all2947online canonical pairs' if name=='full' else 'Exactly111 newly added online keys; native sample set empty. This is a font probe only, not a translated native UI or normal registration proof.','catalog_status':'All111 additions received actual semantic/editorial/adversarial final rereads; older values carried by separate exact-equality proof.'})
oldbase=ROOT/'artifacts/marathi-localization/integration-bc8';oldn=json.loads((oldbase/'previews/mr/native.json').read_text(encoding='utf-8'));oldo=json.loads((oldbase/'previews/mr/online.json').read_text(encoding='utf-8'))
assert n==oldn and len(oldo)==2836 and all(o[k]==v for k,v in oldo.items())
base='64aeda1300e3fcb81b074aef1a9a685498edcf63'
paths=['libgag/src/BidiText.cpp','libgag/src/ui/TextLayout.cpp','libgag/src/DrawableSurfaceCompound.cpp','libgag/include/TrueTypeFont.h','libgag/include/ui/TextLayout.h']+[str(p.relative_to(ROOT)) for p in (ROOT/'data/fonts').rglob('*.ttf')]
unchanged={p:hashlib.sha256(subprocess.check_output(['git','show',base+':'+p])).hexdigest()==sha(ROOT/p) for p in paths};assert all(unchanged.values())
font='libgag/src/TrueTypeFont.cpp';old=subprocess.check_output(['git','show',base+':'+font],text=True);now=(ROOT/font).read_text(encoding='utf-8')
expected=old.replace('geometry for legacy locales. Swahili and Bokmål use the original Latin font,\n\t\t// not the script-font/bidi composition path.','geometry for legacy locales. Swahili, Bokmål and Malay use the original Latin\n\t\t// font, not the script-font/bidi composition path.').replace('scripts || language == "sw" || language == "nb";','scripts || language == "sw" || language == "nb" || language == "ms";')
assert expected==now,'Unexpected font change; no old-matrix carry authorized'
write(BASE/'native-font-carry-inputs.json',{'old_actual_producer':base,'current_selection_source_revision':rev,'actual_native_pairs_equal':3411,'actual_old_online_pairs_equal':2836,'actual_new_online_pairs':111,'unchanged_dependencies':unchanged,'current_dependency_sha256':{p:sha(ROOT/p) for p in paths},'font_old_sha256':hashlib.sha256(old.encode()).hexdigest(),'font_current_sha256':sha(ROOT/font),'font_entire_diff_matches_only_expected_Malay_comment_and_predicate':True,'MR_branch_proof':'Added disjunct language == ms is false for mr, for both scripts=true and scripts=false; all other font code exact. This carries old observations behaviorally, not a claim of current execution.','old_actual_measurements':{'samples':75288,'lines':83542},'new_required_measurements':{'sample_keys':111,'font_sizes':[10,13,20],'scales':[1,2],'viewports':[[1280,900],[390,844]],'expected_samples':1332},'combined_sample_total_is_projection_until_new_reports':76620,'limits':'No old statistics-layout image carried as current: upstream changed narrow Overview geometry. No dynamic formatted-placeholder or OS runtime coverage implied by the font matrix.'})
summary=oldbase/'native-final-summary.json';actual=json.loads(summary.read_text(encoding='utf-8'));assert actual['matrix_samples']==75288 and actual['matrix_lines']==83542
carry=BASE/'native-font-carry-inputs.json';record=json.loads(carry.read_text(encoding='utf-8'));record['old_actual_summary']={'path':str(summary),'sha256':sha(summary),'matrix_reports':actual['reports']};write(carry,record)
harness='tools/TranslationPreviewHarness.cpp';oldh=subprocess.check_output(['git','show',base+':'+harness],text=True);newh=(ROOT/harness).read_text(encoding='utf-8')
a='void fontMatrix(';oldf=oldh[oldh.index(a):oldh.index('\ntemplate',oldh.index(a))];newf=newh[newh.index(a):newh.index('\ntemplate',newh.index(a))];assert oldf==newf
a='std::vector<Sample> samples(';z='struct ProductionMeasurer';assert oldh[oldh.index(a):oldh.index(z)]==newh[newh.index(a):newh.index(z)]
record.update(official_matrix_function_unchanged=True,official_matrix_function_sha256=hashlib.sha256(newf.encode()).hexdigest(),sample_expansion_function_unchanged=True);write(carry,record)
print(inputs)
