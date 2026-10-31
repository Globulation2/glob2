from pathlib import Path
import argparse,datetime,hashlib,json,subprocess,sys,csv
P=Path(__file__).resolve().parent; ROOT=P.parents[2]
a=argparse.ArgumentParser(description='Only root freeze authorization starts full Linux platform check.')
a.add_argument('--run-frozen',action='store_true',required=True)
a.add_argument('--expected-source-tree',required=True)
args=a.parse_args();sys.path.insert(0,str(ROOT/'test'))
from build_provenance import source_identity
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
identity=source_identity(ROOT)
if identity['sourceTreeSha256']!=args.expected_source_tree:raise SystemExit('Source tree differs from authorized frozen revision; no tests started.')
image='glob2-translation-check-tools:local';image_id=subprocess.check_output(['docker','image','inspect',image,'--format','{{.Id}}'],text=True).strip()
catalogs={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'data').glob('texts.*.txt')}
catalogs.update({str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'platform/packages/i18n/locales').glob('*.json')})
cmd=['docker','run','--rm','--init','--name','glob2-translation-full-check','--network','glob2-translation-test','--mount',f'type=bind,source={ROOT},target=/app,readonly','--mount','type=volume,source=glob2-translation-check-node-modules,target=/app/platform/node_modules','--mount',f'type=bind,source={P},target=/evidence','--tmpfs','/tmp:rw,exec,size=4g','--workdir','/app/platform','--env','TEST_DATABASE_URL=postgres://glob2:glob2@db:5432/postgres',image,'bash','/evidence/run-checks.sh']
record={'status':'running','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_identity_before':identity,'source_input_catalog_sha256':catalogs,'dependency_image':image,'dependency_image_id':image_id,'node_modules_volume':'glob2-translation-check-node-modules','node_requirement':'>=22.18','run_script_sha256':sha(P/'run-checks.sh'),'worker_limit':3,'tmpfs':'/tmp:rw,exec,size=4g','repository_readonly':True,'gates':['lint','typecheck','i18n:check','vitest run --maxWorkers=3'],'limits':['No physicaldevice/platformrelease or crossplatformsimulation claims.','Development Docker testdependencies isolated from productiontree.','Independent stages continue after failure for bounded diagnosis; failures are not suppressed.']}
(P/'result.json').write_text(json.dumps(record,indent=2)+'\n')
with (P/'runner.log').open('w') as log:
    result=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
record['docker_exit_code']=result.returncode;record['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();record['source_identity_after']=source_identity(ROOT);record['source_unchanged_during_run']=record['source_identity_before']['sourceTreeSha256']==record['source_identity_after']['sourceTreeSha256']
if (P/'steps.csv').exists():
    with (P/'steps.csv').open() as f: record['steps']=list(csv.DictReader(f))
record['status']='passed' if result.returncode==0 and record['source_unchanged_during_run'] else 'failed-or-source-changed'
record['log_sha256']={str(p.relative_to(P)):sha(p) for p in (P/'logs').glob('*.log')}
(P/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({'status':record['status'],'docker_exit_code':result.returncode,'source_unchanged':record['source_unchanged_during_run']}));sys.exit(0 if record['status']=='passed' else 1)
