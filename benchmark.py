from pathlib import Path
import json,shutil,time,sys
sys.path.insert(0,str(Path.cwd()))
from tools.package_assets import export_assets
def main():
 base=Path('artifacts/parallel-assets/sample');root=base/'source';(root/'data/gfx').mkdir(parents=True,exist_ok=True)
 files=sorted(p for p in Path('data/highres/v1').glob('*.png') if 200_000<=p.stat().st_size<=400_000)[:16]
 assert len(files)==16
 for i,p in enumerate(files):shutil.copyfile(p,root/'data/gfx'/f'sample{i}.png')
 results=[];reference=None
 for repeat in range(2):
  for jobs in (1,4):
   out=base/f'output-{repeat}-{jobs}';cache=base/f'cache-{repeat}-{jobs}'
   start=time.monotonic();audit=export_assets(root,out,cache=cache,jobs=jobs);elapsed=time.monotonic()-start
   hashes=[(r['output'],r['output_sha256']) for r in audit['files']]
   if reference is None:reference=hashes
   assert hashes==reference
   results.append(dict(repeat=repeat,jobs=jobs,seconds=elapsed,files=len(files)))
   print(results[-1],flush=True)
 (base/'results.json').write_text(json.dumps(dict(samples=[str(p) for p in files],results=results,output_hashes=reference),indent=2)+'\n')

if __name__=="__main__":main()
