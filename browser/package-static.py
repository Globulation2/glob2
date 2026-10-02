#!/usr/bin/env python3
"""Package the release with versioned asset names for static bucket hosting."""
import hashlib
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent.parent
source = root / 'build/emscripten/client/release'
destination = root / 'build/browser-static'
destination.mkdir(parents=True, exist_ok=True)
files = {ext: (source / f'index.{ext}').read_bytes()
         for ext in ('html', 'js', 'wasm')}
# Data packages (scons/web_assets.py) are already named by their content and are
# listed inside index.js, so they keep their names under assets/.
packages = sorted((source / 'assets').glob('*.data'))
assert packages, 'Expected data packages in assets/'
version = hashlib.sha256(b''.join(files.values()) + b''.join(p.name.encode() for p in packages)).hexdigest()[:16]
names = {ext: f'index-{version}.{ext}' for ext in ('js', 'wasm')}
script = files['js'].decode()
script = script.replace('"index.wasm"', f'"{names["wasm"]}"')
(destination / names['js']).write_text(script)
(destination / names['wasm']).write_bytes(files['wasm'])
(destination / 'assets').mkdir(exist_ok=True)
for package in packages:
    shutil.copyfile(package, destination / 'assets' / package.name)
html = files['html'].decode().replace('src="index.js"', f'src="{names["js"]}"')
html = html.replace('src=index.js>', f'src="{names["js"]}">')
assert names['js'] in html, 'Expected Emscripten script tag'
(destination / 'index.html').write_text(html)
print(f'Packaged {version} in {destination}')
