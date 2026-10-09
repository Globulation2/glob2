import json,sys
dirs=sys.argv[1:] or ['base','v5']
import glob,os
for tag in dirs:
  for f in sorted(glob.glob(f'artifacts/lava/{tag}/*.json')):
    j=json.load(open(f))
    cq=j.get('canonical_quality')
    if not cq: print(tag, os.path.basename(f), 'FAILED'); continue
    rows=[]
    for c in cq['colonies']:
      r=c['raw']; R=r['resources']
      rows.append('w%s/wd%s/st%s/sites%d' % (r['wheat_distance'], r['wood_distance'], R['stone']['nearest_gather_distance'], r['build_sites_4x4']))
    st=j['resources']['types'].get('stone',{}).get('coverage',{}).get('tiles')
    print(f'{tag:5} {os.path.basename(f):10} fair {cq["fairness"]:.3f} stoneTiles {st} sites {j["space"]["build_sites_4x4"]} |', ' '.join(rows))
