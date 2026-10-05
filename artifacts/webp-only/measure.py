from pathlib import Path
import hashlib, json, sys
sys.path.insert(0,str(Path.cwd()/'scons'))
import web_assets
root=Path.cwd()
result={}
for name in ('native','web'):
    audit=json.loads((root/f'artifacts/webp-only/{name}.json').read_text())
    records=[r for r in audit['files'] if r['source'].endswith('.png')]
    result[name]={'total_bytes':audit['output_bytes'], 'image_source_bytes':sum(r['source_bytes'] for r in records),
                  'image_bytes':sum(r['output_bytes'] for r in records), 'lossy_images':sum(r['lossy'] for r in records),
                  'lossless_images':sum(not r['lossy'] for r in records), 'files':len(audit['files'])}
    pairs=[]
    for r in audit['files']:
        target=root/f'artifacts/webp-only/{name}'/r['output']
        assert hashlib.sha256(target.read_bytes()).hexdigest()==r['output_sha256']
        if not r['source'].endswith('.png'): continue
        source=root/r['source']
        if 'packed_from' in r:
            source=next(p for p in (root/'build/asset-cache').glob('sheet-*/'+Path(r['source']).name)
                        if hashlib.sha256(p.read_bytes()).hexdigest()==r['source_sha256'])
            for frame in r['packed_from']:
                assert hashlib.sha256((root/frame['source']).read_bytes()).hexdigest()==frame['sha256']
        assert hashlib.sha256(source.read_bytes()).hexdigest()==r['source_sha256']
        pairs += [str(source),str(target),'lossy' if r['lossy'] else 'lossless']
    (root/f'artifacts/webp-only/{name}-pairs.txt').write_text('\n'.join(pairs)+'\n')
out=root/'artifacts/webp-only/web-packages'
manifest=web_assets.build(root,out,out/'asset-manifest.js',root/'artifacts/webp-only/web')
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
substitutes=web_assets.plan(root,root/'artifacts/webp-only/web')[2]
for entry in manifest['packages']:
    for part in entry['parts']:
        blob=(out/part['url']).read_bytes()
        assert hashlib.sha256(blob).hexdigest()[:16] in part['url']
        for path,start,end in part['files']:
            assert blob[start:end]==web_assets.contents(root,entry['name'],path.lstrip('/'),substitutes,root/'artifacts/webp-only/web')
result['browser_packages']={e['name']:e['size'] for e in manifest['packages']}
result['browser_substitutes']=substitutes
result['browser_images_bytes']=sum(len(web_assets.contents(root,name,path,substitutes,root/'artifacts/webp-only/web'))
                                  for name,paths in web_assets.plan(root,root/'artifacts/webp-only/web')[0].items()
                                  for path in paths if path.endswith(('.png','.webp')))
(root/'artifacts/webp-only/sizes.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
