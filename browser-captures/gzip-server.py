"""Evidence-only gzip negotiation layered on the real browser isolation handler."""
from pathlib import Path
import importlib.util
from http.server import ThreadingHTTPServer
from functools import partial
from urllib.parse import urlsplit, urlunsplit
root = Path(__file__).resolve().parents[3]
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
            self.gzip_selected = True
            self.original_type = self.guess_type(str(path))
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
ThreadingHTTPServer(('127.0.0.1', 42771), partial(Handler, directory=str(root / 'artifacts/bulgarian/browser-static-release'))).serve_forever()
