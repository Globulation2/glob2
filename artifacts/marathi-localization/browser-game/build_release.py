"""Official package build authorized at frozen runtime revision; QA waits for golden commit."""
from pathlib import Path
import gzip,json,os,shutil,subprocess,time
ROOT=Path(__file__).resolve().parents[3]; OUT=Path(__file__).resolve().parent
expected=os.environ['GLOB2_EXPECTED_RELEASE_REVISION']
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
assert revision==expected,(revision,expected)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()
assert shutil.disk_usage(ROOT).free>=10*1024**3
sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39')
prefix=Path('/home/bradley/.local/share/glob2/development/dependencies/build-a67f9f075109cd87f6160634/prefix')
assert (sdk/'emsdk_env.sh').is_file() and (prefix/'lib/libavcodec.a').is_file()
command=['/usr/bin/scons','target=web','release=1','web_variant=both','web-package','-j4','emsdk='+str(sdk)]
env_updates={'GLOB2_DEV_HOME':str(ROOT/'artifacts/marathi-localization/private-web-store'),'GLOB2_RECORDING_PREFIX':str(prefix),'EMSDK':str(sdk)}
allowed=['test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt']
import hashlib
tracked=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).split(b'\0')
inputs={}
for name in tracked:
 if not name:continue
 name=os.fsdecode(name);file=ROOT/name
 if name not in allowed and file.is_file():inputs[name]=hashlib.sha256(file.read_bytes()).hexdigest()
(OUT/'frozen-runtime-inputs.json').write_text(json.dumps({'revision':revision,'excluded_only':allowed,'sha256_by_path':inputs},indent=2)+'\n')
record={'revision':revision,'compile_producer_revision':revision,'packaging_revision':revision,'state':'running','command':command,'cwd':str(ROOT),'environment':env_updates,'log':str((OUT/'release-build.log.gz').relative_to(ROOT)),'started_at_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'storage_controls':{'minimum_initial_free_bytes':10*1024**3,'maximum_uncompressed_log_bytes':128*1024**2}}
def save(): (OUT/'release-build.json').write_text(json.dumps(record,indent=2)+'\n')
with gzip.open(OUT/'release-build.log.gz','wb') as log:
 process=subprocess.Popen(command,cwd=ROOT,env=dict(os.environ,**env_updates),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 record['pid']=process.pid;save();count=0
 try:
  for data in iter(lambda:process.stdout.read1(65536),b''):
   count+=len(data)
   if count>128*1024**2:raise RuntimeError('Build log allowance exceeded')
   log.write(data);log.flush()
  code=process.wait()
 except BaseException:
  process.terminate();process.wait();record['state']='interrupted';save();raise
record.update(state='complete' if code==0 else 'failed',exit_code=code,uncompressed_log_bytes=count,finished_at_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()));save()
raise SystemExit(code)
