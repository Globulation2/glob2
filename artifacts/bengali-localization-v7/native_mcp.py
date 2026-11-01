"""One explicit local UI call per invocation; never automatically retry input."""
from pathlib import Path
import urllib.request,json,sys,base64,time,os
out=Path(os.environ.get('GLOB2_QA_UI_DIR',str(Path(__file__).resolve().parent/'native-interactive'))).resolve();assert out.is_relative_to(Path(__file__).resolve().parent);run=json.loads((out/'run.json').read_text());url=f"http://127.0.0.1:{run['port']}/mcp"
method=sys.argv[1];args=json.loads(sys.argv[2]) if len(sys.argv)>2 else {};label=sys.argv[3] if len(sys.argv)>3 else method
if method=='initialize':
 payload={'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'Bengali local rendering QA','version':'1'}}}
else:
 if method not in ['ui_status','ui_screenshot','ui_acquire_control','tools/list']:
  session=json.loads((out/'private-session.json').read_text());args['token']=session['token']
 payload={'jsonrpc':'2.0','id':1,'method':'tools/list' if method=='tools/list' else 'tools/call','params':{} if method=='tools/list' else {'name':method,'arguments':args}}
request=urllib.request.Request(url,json.dumps(payload,ensure_ascii=False).encode(),{'Content-Type':'application/json','Accept':'application/json, text/event-stream','MCP-Protocol-Version':'2025-11-25'})
with urllib.request.urlopen(request,timeout=20) as response:reply=json.load(response)
result=reply.get('result',{});data=result.get('structuredContent',{})
if method=='ui_acquire_control' and 'token' in data:(out/'private-session.json').write_text(json.dumps({'token':data['token']})+'\n')
for block in result.get('content',[]):
 if block.get('type')=='image':(out/(label+'.png')).write_bytes(base64.b64decode(block['data']))
clean=json.loads(json.dumps(reply));res=clean.get('result',{})
for block in res.get('content',[]):
 if block.get('type')=='image':block['data']='PNG saved separately'
 if block.get('type')=='text':
  try:
   value=json.loads(block['text']);value.pop('token',None);block['text']=json.dumps(value)
  except (ValueError,AttributeError):pass
if isinstance(res.get('structuredContent'),dict):res['structuredContent'].pop('token',None)
request_args={k:v for k,v in args.items() if k!='token'}
(out/(label+'.json')).write_text(json.dumps({'method':method,'arguments':request_args,'response':clean},ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'result':{k:v for k,v in data.items() if k!='token'},'error':reply.get('error'),'isError':result.get('isError')},ensure_ascii=False))
if reply.get('error') or result.get('isError'):raise SystemExit(1)
