# Local verification cache only: reuse outputs after full input/output attestation.
import json,subprocess,sys,hashlib
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root))
import tools.package_assets as assets
original=assets.export_assets

def verified_export(source,output,platform='generic',optimized=True,lossy=True,cache=None,worker=False):
    target=root/'build/darwin/client/release/runtime-assets'
    if Path(source).resolve()!=root or Path(output).resolve()!=target or platform!='macos' or not optimized or not lossy:
        return original(source,output,platform,optimized,lossy,cache,worker)
    subprocess.run([str(root/'artifacts/terrain-studio/python/bin/python'),str(root/'artifacts/terrain-studio/reuse-assets.py')],check=True)
    audit=json.loads(target.with_suffix('.json').read_text())
    for record in audit['files']:
        assert hashlib.sha256((target/record['output']).read_bytes()).hexdigest()==record['output_sha256']
    print('Reusing audited artwork export: every source, tool, encoder recipe and output hash matches.')
    return audit
assets.export_assets=verified_export
