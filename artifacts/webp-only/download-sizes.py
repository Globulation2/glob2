from pathlib import Path
import gzip,json,sys,tempfile
root=Path.cwd();sys.path.insert(0,str(root));sys.path.insert(0,str(root/'scons'))
import web_assets
from tools.package_assets import export_assets
out=root/'artifacts/webp-only/download-sizes.json'
with tempfile.TemporaryDirectory(prefix='glob2-png-baseline-') as temporary:
 original=Path(temporary)/'original'
 export_assets(root, original, platform='web', optimized=False)
 results={}
 for label,tree in [('original_png',original),('webp',root/'artifacts/webp-only/web')]:
  packages,_,substitutes=web_assets.plan(root,tree)
  rows={}
  for name,paths in packages.items():
   data={p:web_assets.contents(root,name,p,substitutes,tree) for p in paths}
   sizes={p:len(v) for p,v in data.items()}
   groups=[paths] if name=='core' else web_assets.split(paths,sizes,web_assets.PART_BYTES)
   row={'raw':0,'gzip':0}
   for group in groups:
    blob=b''.join(data[p] for p in group)
    row['raw']+=len(blob);row['gzip']+=len(gzip.compress(blob,compresslevel=9,mtime=0))
   rows[name]=row
  results[label]={'packages':rows,'raw':sum(r['raw'] for r in rows.values()),'gzip':sum(r['gzip'] for r in rows.values())}
 # The incremental WebP-only change compared with previous four-way Q90 selection.
 manifest=json.loads((root/'artifacts/q90/web-packages/manifest.json').read_text())
 old=sum(len(gzip.compress((root/'artifacts/q90/web-packages'/part['url']).read_bytes(),compresslevel=9,mtime=0)) for entry in manifest['packages'] for part in entry['parts'])
 results['previous_q90_gzip']=old
 # One serial runtime download, common to both asset comparisons. Baseline runtime
 # is not rebuilt, so this is explicitly an unchanged-runtime comparison.
 runtime=['index.js','index.wasm','loader.js']
 directory=root/'build/emscripten/client/release'
 results['current_serial_runtime_gzip']=sum(len(gzip.compress((directory/p).read_bytes(),compresslevel=9,mtime=0)) for p in runtime if (directory/p).is_file())
 out.write_text(json.dumps(results,indent=2)+'\n')
 print(out.read_text())
