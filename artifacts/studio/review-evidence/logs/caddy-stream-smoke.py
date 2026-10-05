import http.server, threading, subprocess, time, urllib.request, json, pathlib
root=pathlib.Path.cwd()
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream; charset=utf-8')
        self.send_header('Cache-Control','no-cache, no-transform')
        self.end_headers()
        self.wfile.write(b': connected\n\n'); self.wfile.flush()
        time.sleep(0.75)
        self.wfile.write(b'id: 1\ndata: {"stage":"terrain"}\n\n'); self.wfile.flush()
        time.sleep(2)
    def log_message(self,*args): pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',8080),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
log=open(root/'artifacts/map-studio/backend/caddy.log','w')
name='glob2-studio-caddy-smoke'
process=subprocess.Popen(['docker','run','--rm','--name',name,'--network','host','--add-host','platform-api:127.0.0.1','-e','GLOB2_DOMAIN=http://127.0.0.1:18081','-e','GLOB2_REDIRECT_DOMAINS=http://redirect.invalid:18082','-v',f'{root}/deploy/Caddyfile:/etc/caddy/Caddyfile:ro','glob2-caddy:smoke'],stdout=log,stderr=log)
try:
    for i in range(100):
        try:
            urllib.request.urlopen('http://127.0.0.1:2015/livez',timeout=.2); break
        except Exception: time.sleep(.1)
    start=time.monotonic()
    with urllib.request.urlopen('http://127.0.0.1:18081/api/v1/map-studio/threads/test/events',timeout=5) as response:
        first=response.readline().decode().strip(); first_time=time.monotonic()-start
        while True:
            line=response.readline().decode().strip()
            if line.startswith('id:'): break
        event_time=time.monotonic()-start
    result={'first':first,'first_seconds':round(first_time,3),'event_seconds':round(event_time,3),'upstream_closes_after_seconds':2.75,'proxy':'repository deploy/Caddyfile through glob2-caddy:smoke'}
    assert first==': connected' and first_time<.6 and event_time<1.5,result
    print(json.dumps(result,indent=2))
    (root/'artifacts/map-studio/backend/caddy-stream.json').write_text(json.dumps(result,indent=2)+'\n')
finally:
    subprocess.run(['docker','stop',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    process.wait(timeout=10); server.shutdown(); log.close()
