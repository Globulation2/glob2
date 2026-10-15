#!/usr/bin/env python3
"""Explicitly authorized post-PG448 evidence copy/verify only. Never compress or publish."""
from pathlib import Path
import argparse,hashlib,json,os,shutil
F=Path(__file__).resolve().parents[1];O=Path(__file__).resolve().parent
read=lambda p:json.loads(Path(p).read_text())
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def retired(x):
 try:s=(Path('/proc')/str(x['pid'])/'stat').read_text().split(') ',1)[1].split();return s[0]=='Z'or s[19]!=str(x['starttime'])
 except FileNotFoundError:return True
def clean():
 for name in ('complete.json','results/receipt.json'):assert(O/name).is_file()
 assert read(O/'complete.json')['runs']==448 and read(O/'complete.json')['state']=='stopped_after_complete_playground'
 assert read(O/'results/receipt.json')['state']=='playground448_exported_stopped_awaiting_user'
 for x in read(O/'complete.json')['retired_processes']:assert retired(x)
 for name in ('analysis-launch.json','watch-launch.json'):
  x=read(O/name);assert retired(x),('Evidence writer not retired',name)
 for p in Path('/proc').glob('[0-9]*'):
  try:state=(p/'stat').read_text().split(') ',1)[1].split()[0];comm=(p/'comm').read_text().strip()
  except OSError:continue
  assert state=='Z'or not comm.startswith(('glob2','cc1','gcc','g++','clang','lto1','c++')),'Native/compiler still active'

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--selection',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--execute-after-completion',action='store_true');args=a.parse_args();assert args.execute_after_completion and not args.output.exists();clean()
 selection=read(args.selection);assert selection['state']=='resolved'and not selection['missing_mandatory']and not selection['manifest_conflicts']and selection['qualification_context_count']==280
 assert len({x['portable']for x in selection['files']})==selection['file_count'];args.output.mkdir(parents=True);manifest=[]
 try:
  for item in selection['files']:
   clean();src=Path(item['source']);rel=Path(item['portable']);assert not rel.is_absolute()and '..'not in rel.parts
   assert not any(x in src.parts for x in ('.git','profile','profiles','__pycache__'))
   with src.open('rb')as f:magic=f.read(4)
   assert magic not in (b'\x7fELF',b'\x00asm')and not magic.startswith(b'MZ'),src
   before=src.stat();h=sha(src)
   if item['declared_sha256']:assert h==item['declared_sha256'],src
   for name in item['source_aliases']:assert sha(Path(name))==h,(src,name)
   dst=args.output/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);assert sha(dst)==h and sha(src)==h
   after=src.stat();assert(before.st_size,before.st_mtime_ns,before.st_ctime_ns)==(after.st_size,after.st_mtime_ns,after.st_ctime_ns)
   manifest.append({**item,'sha256':h})
  # Extra portable indexes are new generated material, never replacements for
  # immutable raw receipts or captured source files.
  index=args.output/'index';index.mkdir(exist_ok=True)
  result={'state':'materialized_verified_not_compressed_not_published','selection_sha256':sha(args.selection),'materializer_sha256':sha(Path(__file__)),'files':manifest,'file_count':len(manifest),'bytes':sum(x['bytes']for x in manifest),'limits':selection['limits'],'current_source_reconstruction':selection['source_reconstruction'],'qualification_contexts':selection['qualification_contexts']}
  (index/'content-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
  verifier="""from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'index/content-manifest.json').read_text())
for row in m['files']:
 with(root/row['portable']).open('rb')as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256'],row['portable']
print('Every selected byte verified; omitted checksum/replay/binary bodies are not verified by this bundle')
"""
  (index/'verify.py').write_text(verifier)
  (args.output/'README.txt').write_text('Playground448 evidence:32seeds x14designs, unrestrictedauto32 only. Stoppedbyuser; noOazis/waves/56continuation. Open results/index.html; full metrics comparisons.csv, every-game.csv, source reconstruction and all selected file hashes in index/content-manifest.json. Run python3 index/verify.py (Python3.11+). Current source overlays applied LAST on pinned054, original patches/repair history separate. Timed telemetryOff gives winner/tick outcomes, not448wholegamechecksums;280boundedqualification compact records/provenance retained butmassGCS/replay/binary bodies omitted. Known2baselineenginefailures and unexplainedoriginalbaretimeout remain. This local filetree is not published; accessibleimmutableURLs/PR1185 links require root publication.\n')
  (index/'generated-index-hashes.json').write_text(json.dumps({name:sha(args.output/name)for name in ('README.txt','index/content-manifest.json','index/verify.py')},indent=2)+'\n')
  clean();print(json.dumps({'state':result['state'],'output':str(args.output),'files':len(manifest),'bytes':result['bytes']}))
 except BaseException:
  (args.output/'INCOMPLETE.txt').write_text('Materialization failed; preserve this directory, never publish/reuse.\n');raise
if __name__=='__main__':main()
