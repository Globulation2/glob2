"""Create immutable, non-overlapping indices from completed actual capture jobs only."""
from pathlib import Path
import json,hashlib,struct,sys
R=Path(__file__).resolve().parents[3];B=R/'artifacts/bulgarian';O=B/'native-captures/v9-final';assert len(sys.argv)==2 and sys.argv[1].isdigit();n=sys.argv[1];idx=O/('review-index-'+n+'.json');assert not idx.exists();snap=O/('review-receipts-'+n);snap.mkdir();seen=set()
for p in O.glob('review-index-*.json'):seen|={x['path']for x in json.loads(p.read_text())['images']}
images=[];jobs=[]
for receipt in [*sorted(O.glob('surfaces-shard-*/execution.json')),O/'schools/execution.json',O/'api/execution.json']:
 if not receipt.exists():continue
 raw=receipt.read_bytes();data=json.loads(raw);copy=snap/(receipt.parent.name+'.json');copy.write_bytes(raw)
 for job in data['jobs']:
  if job['returncode']!=0:continue
  paths=[p for p in sorted((receipt.parent/job['tag']).glob('*.png'))if str(p.resolve())not in seen]
  if not paths:continue
  jobs.append({'tag':job['tag'],'receipt_snapshot':str(copy.relative_to(R)),'returncode':0,'images':len(paths)})
  for p in paths:
   raw=p.read_bytes();w,h=struct.unpack('>II',raw[16:24]);images.append({'path':str(p.resolve()),'repository_relative_path':str(p.relative_to(R)),'sha256':hashlib.sha256(raw).hexdigest(),'width':w,'height':h,'job':job['tag']})
assert images
frozen=json.loads((B/'v9-frozen-inputs.json').read_text());receipt=json.loads((B/'native-v9-qa/binary-handoff.json').read_text());data={'status':'Immutable completed actual V9 UI subset; independent visual review is separate','compiled_revision':frozen['compiled_revision'],'final_source_revision':frozen['final_source_revision'],'source_sha256':frozen['source_sha256'],'catalog_sha256':frozen['catalog_sha256'],'binary_sha256':receipt['binaries']['TranslationPreviewHarness.capture']['sha256'],'jobs':jobs,'images':images,'counts':{'completed_jobs':len(jobs),'images':len(images)},'review_instruction':'Open actual PNGs, assess visible Bulgarian text/glyphs/clipping/wrapping/controls; record actual inspected image paths/hashes and findings. Do not infer physical-device rendering, hidden controls, or unvisited states.'};idx.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n');idx.chmod(0o444);print(str(idx.relative_to(R)),data['counts'],'SHA256',hashlib.sha256(idx.read_bytes()).hexdigest())
