"""Ignored current-harness capture extension; run only after native writers exit."""
from pathlib import Path
import gzip,hashlib,json,os,shlex,struct,subprocess,shutil
ROOT=Path(__file__).resolve().parents[4];B=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
info=json.loads((B/('build-ready.json' if (B/'build-ready.json').exists() else 'frozen-inputs.json')).read_text(encoding='utf-8'))
info.setdefault('environment_prefixes',json.loads((ROOT/'artifacts/marathi-localization/integration-bc8/native/build-env.json').read_text(encoding='utf-8')))
assert json.loads((B/'official-build-command.json').read_text())['exit_code']==0
assert json.loads((B/'focused-object-build-command.json').read_text())['exit_code']==0
env=dict(os.environ);env.update(info['environment_prefixes'])
source=B/'ExtraPreviewHarness-current.cpp'
# The finalized successful official build log records the exact executed recipe and names
# its exact production objects/libraries. No reconstructed flags or SDK copies.
lines=gzip.open(B/'official-build.log.gz','rt',encoding='utf-8').read().splitlines()
compileline=next(x for x in lines if 'program-tools_TranslationPreviewHarness.o -c ' in x)
compile=shlex.split(compileline);compile[compile.index('-o')+1]=str(B/'ExtraPreviewHarness-current.o');compile[compile.index('tools/TranslationPreviewHarness.cpp')]=str(source)
link=shlex.split(next(x for x in lines if x.startswith('g++ -o ') and '/TranslationPreviewHarness ' in x));link[link.index('-o')+1]=str(B/'ExtraPreviewHarness-current-original');i=next(i for i,x in enumerate(link) if x.endswith('/program-tools_TranslationPreviewHarness.o'));link[i]=str(B/'ExtraPreviewHarness-current.o')
record={'production_objects_producer_revision':info['revision'],'current_source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'actual_variant_source_sha256':sha(source),'official_harness_source_sha256':sha(ROOT/'tools/TranslationPreviewHarness.cpp'),'variant_compile_command':compile,'variant_link_command':link,'scope':'Capture-only navigation of real production stats with actual loaded game history, enlarged-text Overview geometry and normal registered resources. No injected simulation values, no completed-match claim.'}
for name,cmd in [('variant-compile',compile),('variant-link',link)]:
 with (B/(name+'.log')).open('w',encoding='utf-8') as log:r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
 assert r.returncode==0,name

def elf(p):
 d=p.read_bytes();h=struct.unpack_from('<16sHHIQQQIHHHHHH',d);assert h[0][4:6]==b'\x02\x01';ss=[struct.unpack_from('<IIQQQQIIQQ',d,h[6]+i*h[11]) for i in range(h[12])];names=ss[h[13]];strings=d[names[4]:names[4]+names[5]];alloc={}
 for s in ss:
  if s[2]&2:
   name=strings[s[0]:].split(b'\0',1)[0].decode();alloc[name]={'address':s[3],'type':s[1],'flags':s[2],'size':s[5],'alignment':s[8],'sha256':hashlib.sha256(d[s[4]:s[4]+s[5]]).hexdigest() if s[1]!=8 else 'NOBITS'}
 return {'path':str(p),'sha256':hashlib.sha256(d).hexdigest(),'bytes':len(d),'entry':h[4],'program_headers_sha256':hashlib.sha256(d[h[5]:h[5]+h[9]*h[10]]).hexdigest(),'allocated_sections':alloc}
proof={}
for name,original in [('official',ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/test/TranslationPreviewHarness'),('variant',B/'ExtraPreviewHarness-current-original')]:
 dest=B/(name+'-capture-stripped');shutil.copy2(original,dest);cmd=['strip','--strip-all',str(dest)];subprocess.run(cmd,check=True);a,z=elf(original),elf(dest)
 assert all(a[k]==z[k] for k in ('entry','program_headers_sha256','allocated_sections'))
 proof[name]={'original':a,'executed_copy':z,'strip_command':cmd,'proof':'All SHF_ALLOC section contents/addresses/types/flags/sizes/alignment, program headers and entry are identical; nonruntime ELF/debug metadata differ. Actual binary hashes are recorded separately.'}
record['strip_proofs']=proof
(B/'capture-variant-provenance.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
