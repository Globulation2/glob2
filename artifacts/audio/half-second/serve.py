from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
root=Path.cwd()
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*a,**kw):super().__init__(*a,directory=str(root/'build/emscripten/client/release'),**kw)
    def translate_path(self,path):
        name=path.split('?')[0].removeprefix('/')
        if name in ('music-worker.js','music-output.js'):return str(root/'browser'/name)
        return super().translate_path(path)
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy','same-origin')
        self.send_header('Cross-Origin-Embedder-Policy','require-corp')
        self.send_header('Cache-Control','no-store')
        super().end_headers()
ThreadingHTTPServer(('127.0.0.1',8798),Handler).serve_forever()
