from pathlib import Path
import os,json,signal,time,subprocess,datetime,socket
out=Path(__file__).resolve().parent;v=json.loads((out/'provenance.json').read_text());pid=v['pid'];proc=Path('/proc')/str(pid);assert str((proc/'cwd').resolve())==v['cwd'];argv=(proc/'cmdline').read_bytes().split(b'\0');assert argv[:2]==[v['argv'][0].encode(),b'e2e/server.ts'];assert os.getpgid(pid)==pid
os.killpg(pid,signal.SIGTERM);deadline=time.monotonic()+25
while proc.exists() and (proc/'stat').read_text().split()[2]!='Z':
    if time.monotonic()>deadline:raise RuntimeError('Owned website server did not stop; no further signals sent')
    time.sleep(0.2)
cid=v['owned_postgres_container'];current=json.loads(subprocess.check_output(['docker','inspect',cid]))[0];assert current['Id']==cid and current['Name']=='/glob2-marathi-postgres';subprocess.run(['docker','stop','--time','10',cid],check=True,stdout=subprocess.DEVNULL);after=json.loads(subprocess.check_output(['docker','inspect',cid]))[0];assert not after['State']['Running'];s=socket.socket();listener_gone=s.connect_ex(('127.0.0.1',4587))!=0;s.close();assert listener_gone
(out/'runtime-cleanup.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_revision':v['revision'],'owned_server_pid':pid,'verified_argv':v['argv'],'verified_cwd':v['cwd'],'own_process_group_stopped':True,'website_listener_gone':listener_gone,'owned_postgres_container':cid,'container_stopped':not after['State']['Running'],'ports_before':current['NetworkSettings']['Ports'],'scope':'Only task-owned fresh website QA server and isolated Postgres stopped. Source/build outputs/screenshots preserved; foreignservers/DB untouched.'},indent=2)+'\n');print('Owned website server and QA database stopped.')
