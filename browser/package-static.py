#!/usr/bin/env python3
"""Package the release with versioned asset names for static bucket hosting."""
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parent.parent
source = root / 'build/emscripten/client/release'
destination = root / 'build/browser-static'
destination.mkdir(parents=True, exist_ok=True)
files = {ext: (source / f'index.{ext}').read_bytes()
         for ext in ('html', 'js', 'wasm', 'data')}
version = hashlib.sha256(b''.join(files.values())).hexdigest()[:16]
names = {ext: f'index-{version}.{ext}' for ext in ('js', 'wasm', 'data')}
script = files['js'].decode()
for ext in ('wasm', 'data'):
    script = script.replace(f'"index.{ext}"', f'"{names[ext]}"')
(destination / names['js']).write_text(script)
for ext in ('wasm', 'data'):
    (destination / names[ext]).write_bytes(files[ext])
html = files['html'].decode().replace('src="index.js"', f'src="{names["js"]}"')
html = html.replace('src=index.js>', f'src="{names["js"]}">')
assert names['js'] in html, 'Expected Emscripten script tag'
(destination / 'index.html').write_text(html)
print(f'Packaged {version} in {destination}')
