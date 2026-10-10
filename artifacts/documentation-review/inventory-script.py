import importlib.util, json, subprocess
from pathlib import Path
root=Path.cwd(); baseline='3f67899b0899780efb4c309d111852b1e9f267ff'
original=set((root/'docs/.work/documentation-inventory.txt').read_text().splitlines())
original.update(subprocess.check_output(['git','ls-tree','-r','--name-only',baseline,'--','INSTALL','AUTHORS','tools/README','debian/README.Debian','debian/README.source','COPYING']).decode().splitlines())
mapping={}
for name in ['contributor-mapping.json','domain-mappings.json','operations-mappings.json']:
    mapping.update(json.loads((root/'docs/.work'/name).read_text()))
def destination(key):
    seen=set()
    while key not in seen:
        seen.add(key)
        if key in mapping: result=mapping[key]
        else:
            file, sep, anchor=key.partition('#')
            if file not in mapping: break
            result=mapping[file]+(sep+anchor if sep else '')
        if result==key: break
        key=result
    return key
changes=subprocess.check_output(['git','diff','--name-only',baseline,'HEAD']).decode().splitlines()
rows=[]
for name in sorted(original):
    exemption=None
    if name.startswith(('third_party/','test/third_party/')) and name!='third_party/README.md': exemption='Dependency/vendor notices; retain upstream/legal content'
    elif name in ('COPYING','AUTHORS') or 'LICENSE' in Path(name).name or 'license' in Path(name).name.lower(): exemption='Legal/attribution text; retain authoritative notices'
    elif name.startswith('fastlane/'): exemption='Store metadata, outside editorial scope'
    elif name.startswith(('data/texts.','data/keyboard-','data/nicowar','data/pal.','test/data/','test/fixtures/','test/maxima/fixtures/')) or (name.startswith('test/') and name.endswith('.txt')) or name.endswith('requirements.txt') or 'requirements-' in name or name=='requirements-dev.txt' or name.endswith('frames.txt') or name=='campaigns/Tutorial_Campaign.txt': exemption='Runtime data, dependency declarations or fixture contracts; preserve source ownership'
    elif name=='CLAUDE.md' or name.startswith('.claude/'): exemption='Tracked alias; preserve symlink'
    target=destination(name)
    sections={old:destination(old) for old in mapping if old.startswith(name+'#') and destination(old)!=old}
    if exemption: action='retain/exclude'; note=exemption
    elif name in ('docs/development/legacy-architecture.txt','docs/map-generators/FRACTAL_VALIDATION.md'): action='delete'; note='Obsolete historical guide/report removed; durable replacement/extraction at '+target
    elif not (root/target.split('#')[0]).exists(): action='delete'; note='Retired historical material; extraction and rationale in owner ledger'
    elif sections: action='split/rewrite'; note='Section migration destinations recorded below'
    elif target!=name: action='move/rewrite'; note='Canonical destination; see owner source review'
    elif name in changes: action='rewrite'; note='Navigation, factual or editorial correction'
    else: action='retain'; note='Existing focused contract/reference; see domain source ledger where applicable'
    rows.append(dict(original=name,disposition=action,destination=None if action=='delete' else target,notes=note,sections=sections,extracted_to=target if action=='delete' and target!=name else None))
(root/'artifacts/documentation-review/inventory.json').write_text(json.dumps(dict(baseline=baseline,documents=rows),indent=2)+'\n')
(root/'artifacts/documentation-review/section-mappings.json').write_text(json.dumps(mapping,indent=2)+'\n')
for name in ['contributor-ledger.md','domain-ledger.md','operations-ledger.md']:
    (root/'artifacts/documentation-review'/name).write_text((root/'docs/.work'/name).read_text())
print(f'Accounted for {len(rows)} original documentation/data/notice paths; {sum(len(r["sections"]) for r in rows)} section migrations.')
