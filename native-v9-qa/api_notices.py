from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
import threading,json,hashlib,os,sys,subprocess,time
root=Path.cwd();binary=Path(sys.argv[1]).resolve();kit=root/'artifacts/bulgarian/v9-full-render-input/render-input/bg';base=root/'artifacts/bulgarian/native-captures/v9-final/api';base.mkdir(parents=True,exist_ok=True);catalog=json.loads((root/'platform/packages/i18n/locales/bg.json').read_text());jobs=[]
assert len(sys.argv)==4 and sys.argv[2]=='--handoff','BINARY --handoff REVISION required'
from run_phase import guard
guard(binary,sys.argv[3])
assert not (base/'execution.json').exists(),'Never overwrite prior API evidence'
fixtures=[('known',{'code':'not_found','message':'Synthetic English not-found diagnostic must not be shown.','messageKey':'{value0} not found','messageParams':{'value0':'ИИ preview-ai'}},catalog['{value0} not found'].replace('{value0}','ИИ preview-ai')),('unknown',{'code':'synthetic_failure','message':'Synthetic raw English diagnostic must not be shown.','messageKey':'synthetic.unknown.localization.key','messageParams':{}},catalog['Something went wrong. Please try again.'])]
for presentation,w,h in [('desktop',1280,1024),('phone',480,960)]:
 for kind,body,expected in fixtures:
  tag=presentation+'-api-'+kind;output=base/tag;assert not output.exists(),'Fresh API output required';output.mkdir();receipt=[];profile=output/'profile';profile.mkdir(exist_ok=True)
  class Handler(BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def reply(self):
    count=int(self.headers.get('Content-Length','0'));assert count<1024*1024
    if count:self.rfile.read(count)
    status=503
    if self.path=='/api/v1/auth/guest':
     status=200;response={'account':{'id':'00000000-0000-4000-8000-000000000001','displayName':'Synthetic local fixture','kind':'guest','role':'user'},'tokens':{'accessToken':'synthetic-local-access-token','refreshToken':'synthetic-local-refresh-token','accessTokenExpiresAt':'2030-01-01T00:00:00Z','refreshTokenExpiresAt':'2030-01-01T00:00:00Z'},'deviceCredential':'f'*43}
    elif self.path.startswith('/api/v1/ais?'):response=body
    else:response={'code':'unavailable','message':'Synthetic local fixture has no realtime service.'}
    encoded=json.dumps(response).encode();receipt.append({'method':self.command,'path':self.path,'status':status,'response':response});self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(encoded)));self.send_header('Connection','close');self.end_headers();self.wfile.write(encoded)
   do_GET=reply;do_POST=reply
  server=ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();origin=f'http://127.0.0.1:{server.server_port}'
  env=dict(os.environ,LD_LIBRARY_PATH=str(root/'artifacts/bulgarian/sdl3/prefix/lib')+':'+os.environ.get('LD_LIBRARY_PATH',''),GLOB2_USER_DATA_DIR=str(profile/'user'),XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'),SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',GLOB2_RENDERER='software',GLOB2_TRANSLATION_PREVIEW_HTTP_ORIGIN=origin,GLOB2_TRANSLATION_PREVIEW_EXPECT_NOTICE=expected)
  env['XDG_SESSION_TYPE']='x11';env['SDL_VIDEO_DRIVER']='x11'
  for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(key,None)
  if presentation=='phone':env['GLOB2_MOBILE_UI']='1'
  else:env.pop('GLOB2_MOBILE_UI',None)
  cmd=['xvfb-run','-a','-s','-screen 0 1600x1400x24',str(binary),'screen',str(root),str(kit),str(output),str(w),str(h),'settings-ais'];start=time.monotonic();print('START',tag,flush=True)
  try:
   with (output/'capture.log').open('w')as log:
    try:rc=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600).returncode
    except subprocess.TimeoutExpired:rc='timeout'
  finally:server.shutdown();server.server_close();thread.join()
  (output/'http-fixture-receipt.json').write_text(json.dumps({'identity':'Controlled localhost HTTP fixture, not production server/account/device coverage','origin':origin,'expected_notice':expected,'fixture_body':body,'requests':receipt},ensure_ascii=False,indent=2)+'\n')
  if rc==0:assert any(x['path'].startswith('/api/v1/ais?') and x['response']==body for x in receipt)
  jobs.append({'tag':tag,'command':cmd,'returncode':rc,'seconds':round(time.monotonic()-start,2),'expected_notice':expected,'receipt':str(output/'http-fixture-receipt.json')});(base/'execution.json').write_text(json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'jobs':jobs},ensure_ascii=False,indent=2)+'\n');print('END',tag,rc,flush=True)
print('COMPLETE',sum(x['returncode']==0 for x in jobs),'/',len(jobs),flush=True)
