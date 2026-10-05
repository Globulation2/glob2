import importlib.util,ipaddress,json,os,pathlib,subprocess,tempfile,uuid
root=pathlib.Path.cwd()
spec=importlib.util.spec_from_file_location('smoke',root/'test/deployment/platform_stack_smoke.py');smoke=importlib.util.module_from_spec(spec);spec.loader.exec_module(smoke)
def cmd(*args,check=True):
 r=subprocess.run(args,capture_output=True,text=True)
 if check and r.returncode:raise RuntimeError(r.stderr)
 return r
for variant in ('before','after'):
 subnet=smoke.free_subnet();proxy=str(subnet.network_address+10);pool=str(list(subnet.subnets(prefixlen_diff=1))[1])
 network='glob2-pool-probe-'+uuid.uuid4().hex[:8];containers=[]
 with tempfile.TemporaryDirectory() as d:
  envfile=pathlib.Path(d)/'.env';envfile.write_text('POSTGRES_PASSWORD=disposableprobe\n')
  compose=root/'deploy/compose.yaml'
  if variant=='before':
   compose=pathlib.Path(d)/'compose.yaml';compose.write_text(cmd('git','show','HEAD^:deploy/compose.yaml').stdout)
  env={k:v for k,v in os.environ.items() if not k.startswith(('GLOB2_','POSTGRES_'))}
  env.update(GLOB2_ENV_FILE=str(envfile),GLOB2_BACKEND_SUBNET=str(subnet),GLOB2_PROXY_ADDRESS=proxy,GLOB2_BACKEND_IP_RANGE=pool)
  config=json.loads(subprocess.check_output(['docker','compose','-f',str(compose),'--env-file',str(envfile),'config','--format','json'],env=env,text=True))
  ipam=config['networks']['backend']['ipam']['config'][0]
  args=['docker','network','create','--internal','--subnet',ipam['subnet']]
  if ipam.get('ip_range'):args+=['--ip-range',ipam['ip_range']]
  args+=[network];cmd(*args)
  try:
   addresses=[]
   for i in range(12):
    name=network+'-'+str(i);containers.append(name)
    cmd('docker','run','-d','--name',name,'--network',network,'--entrypoint','sleep','node:22-bookworm-slim','300')
    addresses.append(json.loads(cmd('docker','inspect',name).stdout)[0]['NetworkSettings']['Networks'][network]['IPAddress'])
   name=network+'-proxy';containers.append(name)
   cmd('docker','run','-d','--name',name,'--network','bridge','--entrypoint','sleep','node:22-bookworm-slim','300')
   result=cmd('docker','network','connect','--ip',proxy,network,name,check=False)
   print(json.dumps({'variant':variant,'networkConfig':ipam,'fixedProxy':proxy,'automaticAddresses':addresses,'proxyConnectExit':result.returncode,'proxyConnectError':result.stderr.strip()}),flush=True)
   if variant=='before':assert result.returncode!=0 and proxy in addresses
   else:assert result.returncode==0 and all(ipaddress.ip_address(a) in ipaddress.ip_network(pool) for a in addresses) and proxy not in addresses
  finally:
   cmd('docker','rm','-f',*containers,check=False);cmd('docker','network','rm',network,check=False)
