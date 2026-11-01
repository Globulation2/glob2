"""Evidence-only gzip negotiation layered on the real browser isolation handler."""
from pathlib import Path
import importlib.util
from http.server import ThreadingHTTPServer
from functools import partial
from urllib.parse import urlsplit, urlunsplit
root = Path(__file__).resolve().parents[3]
import json, hashlib, subprocess
handoff_file = Path(__file__).resolve().parent / 'handoff.json'
assert handoff_file.is_file(), 'Explicit root final handoff is missing'
handoff = json.loads(handoff_file.read_text())
assert handoff.get('authorized_by') == '/root' and handoff.get('execution_authorized') is True and handoff.get('package_verified') is True, 'Final package/execution authorization is pending'
subprocess.run(['/home/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node', '-e', "require('./guard.cjs').guard()"], cwd=Path(__file__).resolve().parent, check=True)
for name, digest in handoff['package_files'].items():
    file = Path(__file__).resolve().parent / 'static' / name
    assert hashlib.sha256(file.read_bytes()).hexdigest() == digest, 'Packaged file changed: ' + name
spec = importlib.util.spec_from_file_location('glob2_serve', root / 'browser/serve.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class Handler(module.Handler):
    def send_head(self):
        self.gzip_selected = False
        original = self.path
        parsed = urlsplit(original)
        mapped = parsed.path
        path = Path(self.translate_path(original))
        if path.is_dir():
            path = path / 'index.html'
            mapped = mapped.rstrip('/') + '/index.html'
        if 'gzip' in self.headers.get('Accept-Encoding', '') and path.is_file() and Path(str(path)+'.gz').is_file():
            self.original_type = super().guess_type(str(path))
            self.gzip_selected = True
            self.path = urlunsplit((parsed.scheme, parsed.netloc, mapped + '.gz', parsed.query, parsed.fragment))
        try:
            return super().send_head()
        finally:
            self.path = original
    def guess_type(self, path):
        return self.original_type if getattr(self, 'gzip_selected', False) else super().guess_type(path)
    def end_headers(self):
        if getattr(self, 'gzip_selected', False):
            self.send_header('Content-Encoding', 'gzip')
        self.send_header('Vary', 'Accept-Encoding')
        super().end_headers()
# Start only after explicit root artifact handoff and package verification.
server = ThreadingHTTPServer(('127.0.0.1', 42771), partial(Handler, directory=str(root / 'artifacts/bulgarian/browser-final-v8/static')))
import os
(Path(__file__).resolve().parent / 'server-owned.json').write_text(json.dumps({'pid': os.getpid(), 'port': 42771, 'revision': handoff['revision'], 'ownership': '/root/browser_final only'}, indent=2) + '\n')
server.serve_forever()
