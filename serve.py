from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
root = Path.cwd()
class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/play/'):
            query = parse_qs(urlparse(self.path).query)
            theme = query.get('theme', ['light'])[0]
            theme = theme if theme in ('light', 'dark') else 'light'
            html = (root / 'browser/shell.html').read_text()
            html = html.replace('<script src="/theme.js">', "<script>localStorage.setItem('glob2-theme', '" + theme + "')</script><script src=\"/theme.js\">")
            html = html.replace('{{{ SCRIPT }}}', "<script>download.started=performance.now()-5000;download.wasmTotal=40000000;download.wasmKnown=true;download.wasm=16000000;showDownload();</script>")
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            self.wfile.write(html.encode())
            return
        super().do_GET()
    def translate_path(self, path):
        path = path.split('?')[0]
        if path in ('/play/', '/play/studio.html'):
            return str(root / 'browser/shell.html')
        if path.startswith('/signin/assets/'):
            return str(root / 'platform/apps/api/src/web/static' / path.rsplit('/', 1)[1])
        return str(root / 'platform/apps/web/public' / path.lstrip('/'))
ThreadingHTTPServer(('127.0.0.1', 8768), Handler).serve_forever()
