#!/usr/bin/env python3
"""Local browser host with the same isolation contract as deployment."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', nargs='?', type=int, default=8765)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--directory', default='build/emscripten/client/release')
    args = parser.parse_args()
    ThreadingHTTPServer((args.bind, args.port), partial(Handler, directory=args.directory)).serve_forever()
