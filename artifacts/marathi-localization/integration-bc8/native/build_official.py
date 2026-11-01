from pathlib import Path
import os,json,subprocess,hashlib
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native';env=dict(os.environ);env.update(json.loads((base/'build-env.json').read_text()));revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();status=subprocess.check_output(['git','status','--porcelain'],text=True);assert revision=='64aeda1300e3fcb81b074aef1a9a685498edcf63' and not status
command=['python3','tools/dev_build.py','translation-preview-harness','-j4'];record={'revision':revision,'source_status':status,'command':command,'dependency_proof':'recording-prefix-validation.json','environment':{k:env[k] for k in ('GLOB2_SDL3_PREFIX','GLOB2_RECORDING_PREFIX')}};path=base/'build-request.json';path.write_text(json.dumps(record,indent=2)+'\n')
with (base/'official-build.log').open('w') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
record['exit_code']=r.returncode;record['log_sha256']=hashlib.sha256((base/'official-build.log').read_bytes()).hexdigest();path.write_text(json.dumps(record,indent=2)+'\n');raise SystemExit(r.returncode)
