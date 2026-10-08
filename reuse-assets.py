import hashlib,json,sys
from pathlib import Path
root=Path.cwd()
manifest=json.loads((root/'artifacts/terrain-studio/current-asset-inputs.json').read_text())
for name,digest in manifest.items():
 assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
output=root/'build/darwin/client/release/runtime-assets'
audit=json.loads(output.with_suffix('.json').read_text())
for record in audit['files']:
 assert hashlib.sha256((output/record['output']).read_bytes()).hexdigest()==record['output_sha256'],record['output']
print(f"Verified {len(manifest)} current source/tool inputs and {len(audit['files'])} freshly exported runtime outputs.")
