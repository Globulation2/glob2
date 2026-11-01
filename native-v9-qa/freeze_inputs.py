"""Authorized source/input freeze; no executable execution. Optional final-only E2E closure is explicit."""
from pathlib import Path
import json,subprocess,sys,hashlib,importlib.util
from run_phase import R,B,write,sha
assert len(sys.argv)in [4,5]and sys.argv[1]=='--freeze','--freeze FINAL_SOURCE_REV REVIEW_PROOF_PATH [COMPILED_REV]'
final=sys.argv[2];proof=Path(sys.argv[3]).resolve();compiled=sys.argv[4]if len(sys.argv)==5 else final
assert proof.is_file()and sha(proof)=='974fe1ddee961d1bdf0359a0c2e79d48df49bb23b4ee262640bebad4fa506567'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()==final
assert not subprocess.check_output(['git','status','--porcelain'],cwd=R,text=True).strip()
allowed=['platform/apps/web/e2e/i18n.spec.ts'];changed=subprocess.check_output(['git','diff','--name-only',compiled,final],cwd=R,text=True).splitlines();assert set(changed)<=set(allowed),changed
patch=subprocess.check_output(['git','diff','--binary','--full-index',compiled,final],cwd=R);patchpath=B/'native-v9-qa/final-source-test-only-closure.patch';patchpath.write_bytes(patch)
closure={'compiled_revision':compiled,'final_source_revision':final,'changed_paths':changed,'allowed_test_only_paths':allowed,'exact_patch_sha256':hashlib.sha256(patch).hexdigest(),'production_cpp_catalog_font_build_inputs_unchanged':True,'claim':'Native executable keeps its actual compiled identity; only the explicitly authorized E2E test file may differ in final source.'};write(B/'native-v9-qa/final-source-test-only-closure.json',closure)
subprocess.run([sys.executable,str(Path(__file__).with_name('prepare.py'))],check=True,cwd=R)
plan=json.loads((B/'native-captures/v9-final/full-font-plan.json').read_text());kit=B/'kit-v8';spec=importlib.util.spec_from_file_location('p',R/'tools/localization/translation_program.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);native,errors=p.CHECKER.read_table(kit/'drafts/bg/texts.bg.txt');assert not errors;online=json.loads((kit/'drafts/bg/bg.json').read_text())
meta=dict(json.loads((B/'kit-v4/render-input/bg/metadata.json').read_text()),source_revision=compiled,candidate_revision=final,final_source_revision=final,compiled_runtime_revision=compiled,source_sha256=plan['candidate_source_sha256'],original_catalog_sha256=plan['candidate_catalog_raw_sha256'],review_proof_sha256=sha(proof),source_closure=closure,runtime_identity_note='Actual compiled native identity retained. Final source may differ only by the bound explicitly authorized E2E test-only patch; no executable relabeling.')
# Never reuse a prior producer's copy identity as current input metadata.
meta.pop('capture_executable_postprocess',None)
meta['rendering_code_sha256']={path:sha(R/path)for path in meta.get('rendering_code_sha256',{})if(R/path).is_file()}
for base,cats,scope in [(B/'v9-full-render-input',{'native':native,'online':online},'FULL final catalog corpus; no delta-only measurement carry'),(B/'v9-probe-input',{'native':{'[settings]':'ѝ Ѝ ѝ Ѝ — „Български език“; «цитат», … ! ?'},'online':{}},'Synthetic font diagnostic, not translated catalog')]:
 out=base/'render-input/bg';out.mkdir(parents=True,exist_ok=True)
 for surface,cat in cats.items():write(out/(surface+'.json'),cat)
 write(out/'metadata.json',dict(meta,scope=scope));write(base/'manifest.json',{'revision':compiled,'final_source_revision':final,'source_sha256':plan['candidate_source_sha256'],'capture_wrapper':'Actual native compiled revision and explicit final test-only source closure; original immutable provider kit manifest is untouched.'})
paths=[R/'data/texts.bg.txt',R/'platform/packages/i18n/locales/bg.json',proof,B/'native-v9-qa/final-source-test-only-closure.json',patchpath]
for base in [B/'v9-full-render-input',B/'v9-probe-input']:paths+=[base/'manifest.json',*sorted((base/'render-input/bg').glob('*.json'))]
write(B/'v9-frozen-inputs.json',{'revision':compiled,'compiled_revision':compiled,'final_source_revision':final,'source_closure':closure,'input_files_sha256':{str(f.relative_to(R)):sha(f)for f in paths},'source_sha256':plan['candidate_source_sha256'],'catalog_sha256':plan['candidate_catalog_raw_sha256'],'review_proof_path':str(proof),'review_proof_sha256':sha(proof),'final_render_values':plan['render_values'],'fresh_render_values':plan['render_values'],'expected_full_font_samples':12*plan['render_values'],'scope':'Actual input freeze only; explicit root binary handoff and execution pending'})
print('Frozen final full corpus',plan['render_values'],'values /',12*plan['render_values'],'fresh samples; source',final,'compiled',compiled)
