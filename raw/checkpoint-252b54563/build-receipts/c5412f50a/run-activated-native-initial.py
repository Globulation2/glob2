import os,json,subprocess,hashlib,xml.etree.ElementTree as ET
from pathlib import Path
root=Path.cwd(); output=root/'artifacts/gpu-offload/native-c5412f50a-activated';output.mkdir(exist_ok=False)
binary=root/'artifacts/gpu-offload/candidates/c5412f50a/glob2-unit-tests'
cases=[('direct-upload','singleton direct seed uploads preserve exact arrays commands and mixed batch staging',1330,{'GLOB2_OPENCL_DIRECT_SEED_UPLOAD':'1'}),('observation-rollback','isolated completed kernels remain observed when transactional output is declined',12,{'GLOB2_TEST_OPENCL_EXECUTION_OBSERVATION_ROLLBACK':'1'}),('direct-upload-rollback','isolated direct seed upload rollback preserves originals after device work',18,{'GLOB2_TEST_OPENCL_DIRECT_UPLOAD_ROLLBACK':'1','GLOB2_OPENCL_DIRECT_SEED_UPLOAD':'1'})]
base={'GLOB2_OPENCL_DEVICE':'0','GLOB2_OPENCL_POLL_US':'0','GLOB2_OPENCL_PROFILE':'0','GLOB2_OPENCL_API_CPU':'0','GLOB2_OPENCL_ACTIVE_EPOCH':'0','GLOB2_OPENCL_PARITY_BOUND':'0','GLOB2_OPENCL_DIRECT_SEED_UPLOAD':'0','GLOB2_GRADIENT_BATCH':'1'}
results=[]
for ident,name,count,changes in cases:
 d=output/ident;d.mkdir();env=dict(os.environ,**base,**changes);env['GLOB2_TEST_ARTIFACTS_ROOT']=str(d)
 command=[str(binary),'-r=xml','--no-breaks=true','--no-colors=true','-ts=OpenCLGradient','-tc='+name]
 with (d/'results.xml').open('w') as xml,(d/'stderr.log').open('w') as err: completed=subprocess.run(command,cwd=root,env=env,stdout=xml,stderr=err,timeout=180)
 doc=ET.parse(d/'results.xml').getroot(); active=[c for c in doc.findall('.//TestCase') if c.get('skipped')!='true']; asserts=doc.find('OverallResultsAsserts'); actual=int(asserts.get('successes')); failures=int(asserts.get('failures'));valid=completed.returncode==0 and len(active)==1 and active[0].get('name')==name and actual==count and failures==0
 r={'case':ident,'expected_assertions':count,'actual_successful_assertions':actual,'failed_assertions':failures,'exit_code':completed.returncode,'active_cases':[c.get('name') for c in active],'valid':valid,'command':command,'environment':dict(base,**changes),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()};results.append(r);(d/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(ident,actual,failures,valid,flush=True)
(output/'summary.json').write_text(json.dumps(results,indent=2)+'\n');raise SystemExit(0 if all(r['valid'] for r in results) else 1)
