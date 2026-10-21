from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.request import urlopen
from pathlib import Path
from threading import Lock
root = Path(__file__).resolve().parent
source = Path('browser/shell.html').read_text()
helper = source[source.index('function browserStartupRenderer('):source.index('// End browser startup renderer.')]
assignment = '    Module.renderer = browserStartupRenderer(preferences, requestedBackend, Module.renderer);'
reconcile = source[source.index('    const preferences = FS.analyzePath(preferencesPath)'):source.index('    const graphics =',source.index('    const preferences = FS.analyzePath(preferencesPath)'))]
cache = {}
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        key = self.path.split('?')[0]
        try:
            if key not in cache:
                with urlopen('https://app.glob2online.com/play'+key,timeout=60) as response:
                    cache[key] = (response.read(),response.headers.get('Content-Type','application/octet-stream'))
            body,kind = cache[key]
            if key in ('/','/index.html') and (root/'enable-fix').exists():
                html=body.decode()
                html=html.replace('function chooseRenderer() {',helper+'\nfunction chooseRenderer() {')
                marker="    const requestedBackend = new URLSearchParams(location.search).get('renderer');\n"
                assert marker in html
                html=html.replace(marker,marker+reconcile)
                body=html.encode()
            self.send_response(200)
            self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(body)))
            self.send_header('Cross-Origin-Opener-Policy','same-origin')
            self.send_header('Cross-Origin-Embedder-Policy','require-corp')
            self.send_header('Cache-Control','no-cache')
            self.end_headers();self.wfile.write(body)
        except Exception as error:
            self.send_error(502,str(error))
ThreadingHTTPServer(('127.0.0.1',8875),Handler).serve_forever()
