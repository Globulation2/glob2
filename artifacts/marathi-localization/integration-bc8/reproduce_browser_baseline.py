"""Read exact recorded master blobs into bounded temporary files and rerun3fixture tests."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,tempfile
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--node',default=shutil.which('node'));args=parser.parse_args();assert args.node
proof=json.loads((HERE/'browser-fixture-baseline-proof.json').read_text());master=proof['master'];rows=[];total=0
with tempfile.TemporaryDirectory(prefix='glob2-marathi-baseline-') as folder:
    temp=Path(folder);assert shutil.disk_usage(temp).free>1024**3
    for item in proof['files']:
        data=subprocess.check_output(['git','show',master+':'+item['path']]);total+=len(data);assert total<64*1024**2;assert hashlib.sha256(data).hexdigest()==item['sha256'];p=temp/item['path'];p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);rows.append({'path':item['path'],'sha256':item['sha256'],'bytes':len(data)})
    argv=[args.node,'--test','--test-reporter=tap',str(temp/'browser/unit/replay-fixture.test.js')]
    result=subprocess.run(argv,capture_output=True,timeout=60);output=result.stdout+result.stderr;assert len(output)<1024**2
    (HERE/'browser-baseline-reproduction.log').write_bytes(output);text=output.decode('utf-8');assert result.returncode==1 and '# tests 3' in text and '# fail 3' in text and 'ENOENT' not in text
(HERE/'browser-baseline-reproduction.json').write_text(json.dumps({'master':master,'node_version':subprocess.check_output([args.node,'--version'],text=True).strip(),'source_files':rows,'input_bytes':total,'input_byte_cap':64*1024**2,'output_byte_cap':1024**2,'timeout_seconds':60,'exit_code':result.returncode,'tests':3,'failures':3,'all_inputs_hash_verified':True,'temporary_inputs_removed':True,'log_sha256':hashlib.sha256(output).hexdigest(),'scope':'Expected exactupstream replayfixture failures, not a passingMRtest. ExtractsimmutableGitblobs; no trackedfiles/network/model/API calls.'},indent=2)+'\n');print('Exactmaster baseline reproduced:3/3fixture testsfail; temporaryinputsremoved.')
