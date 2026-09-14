#!/usr/bin/env python3
"""Opt-in real-binary localhost/SSH pilot; only interrupts this experiment's processes."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
sys.path.insert(0,os.environ.get('GLOB2_PILOT_PACKAGE',str(Path(__file__).resolve().parents[1]/'tools')))
from tournaments.bundles import inspect_bundle
from tournaments.common import atomic_json,file_hash,store_artifact
from tournaments.coordinator import Coordinator
from tournaments.model import job
from tournaments.transport import Transport
from tournaments.transfer import send_file


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',required=True);p.add_argument('--hosts',required=True)
    p.add_argument('--mac-bundle',required=True);p.add_argument('--linux-bundle',required=True)
    p.add_argument('--map',required=True);p.add_argument('--ticks',type=int,default=8192)
    args=p.parse_args();root=Path(args.output).resolve();hosts=json.loads(Path(args.hosts).read_text())
    if (root/'experiment.json').exists():raise ValueError('use a fresh pilot directory')
    mac=inspect_bundle(args.mac_bundle);linux=inspect_bundle(args.linux_bundle)
    source=store_artifact(args.map,root/'artifacts');jobs=[]
    for bundle,count in ((mac,6),(linux,30)):
        for seed in range(count):
            jobs.append(job('game',bundle['id'],inputs={'map':source},seeds={'game':seed+300},
                            config={'players':['cortex','cortex'],'ticks':args.ticks},
                            limits={'timeout_seconds':180},labels={'format':'1v1','block':seed}))
    c=Coordinator.submit(root,{'schema_version':1,'id':'reliability-pilot','jobs':jobs,
                              'settings':{'heartbeat_seconds':1,'lease_seconds':30,'prefetch':2}},[args.mac_bundle,args.linux_bundle])
    events=[]
    def event(kind,**data):
        events.append({'time':time.time(),'event':kind,**data});atomic_json(root/'reports/events.json',events)
        print(kind,json.dumps(data),flush=True)
    transports={h['name']:Transport(h,root/'worker.pyz') for h in hosts}
    def warm(host):
        t=transports[host['name']];status=t.rpc('status');bundle=mac if host.get('transport')=='local' else linux
        archive=c.archive_bundle(bundle['id']);identity=file_hash(archive)
        while not send_file(t,archive,identity):pass
        t.rpc('install',identity=identity,bundle_id=bundle['id'])
        while not send_file(t,root/'artifacts'/source['sha256'],source['sha256']):pass
        return {'host':host['name'],'status':status,'bundle':bundle['id']}
    # Bundle archive creation is sequential; uploads have independent bounded concurrency.
    c.archive_bundle(mac['id']);c.archive_bundle(linux['id'])
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for value in pool.map(warm,hosts):event('host_ready',**value)
    started=time.time()
    offline=next(h for h in hosts if h['name']=='pharaoh-dev-2.local')
    # Start this host first so the disconnection has three concrete buffered leases.
    c.sync_host(offline)
    c.run([h for h in hosts if h!=offline],once=True)
    event('initial_dispatch',status=c.status())
    away_at=time.time();before=transports[offline['name']].rpc('status')
    event('disconnect',host=offline['name'],status=before)
    victim=transports['pharaoh-dev-1.local'];old=victim.rpc('status')['daemon_pid']
    victim.call([victim.python,'-c','import os,signal,sys;os.kill(int(sys.argv[1]),signal.SIGKILL)',str(old)])
    event('worker_terminated',host=victim.name,pid=old)
    injured=transports['pharaoh-dev-3.local']
    script='''import os,pathlib,signal,sqlite3,sys
root=pathlib.Path(sys.argv[1]);db=sqlite3.connect(root/'queue.sqlite')
row=db.execute("SELECT pid FROM queue WHERE state='running' AND experiment=?",(sys.argv[2],)).fetchone()
if row:
 try:os.killpg(row[0],signal.SIGKILL);print(row[0])
 except ProcessLookupError:print('already finished')
else:print('no running process')
'''
    event('attempt_terminated',host=injured.name,process=injured.call([injured.python,'-c',script,injured.root,c.manifest['id']]).decode())
    connected=[h for h in hosts if h!=offline];atomic_json(root/'reports/connected-hosts.json',connected)
    c.close()
    with (root/'reports/coordinator-killed.log').open('w') as log:
        process=subprocess.Popen([sys.executable,str(root/'worker.pyz'),'run',str(root),'--hosts',str(root/'reports/connected-hosts.json')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        time.sleep(3);os.killpg(process.pid,signal.SIGKILL);process.wait()
    event('coordinator_terminated',pid=process.pid)
    c=Coordinator(root);returned=False
    try:
        deadline=time.time()+900
        while time.time()<deadline:
            if not returned and time.time()-away_at>=40:
                # Read-only observation before delivery of controls/results.
                event('buffered_while_disconnected',host=offline['name'],status=transports[offline['name']].rpc('status'))
                returned=True;connected=hosts;event('reconnect',host=offline['name'])
            c.run(connected,once=True)
            states=c.status()['jobs']
            if returned and not states.get('pending') and not states.get('active'):break
            time.sleep(1)
        # Drain late duplicates, including results finished after their replacement.
        for _ in range(15):
            c.run(hosts,once=True,collect_only=True)
            live=[(t.name,a) for t in transports.values() for a in t.rpc('status')['attempts'] if a['experiment']==c.manifest['id']]
            if not live:break
            time.sleep(1)
        status=c.status();rows=[dict(r) for r in c.db.execute('SELECT id,job_id,host,state,category,record FROM attempts')]
        accepted=[r for r in rows if r['state']=='accepted']
        checks={'all_jobs_completed':status['jobs']=={'completed':len(jobs)},
                'single_accepted_per_job':len(accepted)==len(jobs) and len({r['job_id'] for r in accepted})==len(jobs),
                'late_results_retained':any(r['state']=='late' and r['record'] for r in rows),
                'interrupted_attempt_recovered':any(r['category']=='interrupted' for r in rows),
                'worker_restarted':victim.rpc('status')['daemon_pid']!=old,
                'buffered_execution':any(e['event']=='buffered_while_disconnected' and any(a['state'] in ('running','done') and any(b['id']==a['id'] and b['state']=='queued' for b in before['attempts']) for a in e['status']['attempts']) for e in events),
                'expired_jobs_reassigned':any(r['host']!=offline['name'] and any(a['job_id']==r['job_id'] and a['host']==offline['name'] for a in rows) for r in accepted)}
        table=[]
        for h in hosts:
            records=[json.loads(r['record']) for r in rows if r['host']==h['name'] and r['record']]
            success=[r for r in records if r['category']=='success']
            table.append({'host':h['name'],'slots':h['slots'],'completed_attempts':len(success),'accepted':sum(r.get('accepted',False) for r in records),
                          'seconds':sum(r.get('seconds',0) for r in success),'peak_rss_bytes':max((r.get('resource_usage',{}).get('peak_rss_bytes',0) for r in records),default=0),
                          'ticks_per_process_second':sum(r['result']['ticks'] for r in success)/max(.001,sum(r.get('seconds',0) for r in success))})
        report={'passed':all(checks.values()),'checks':checks,'wall_seconds':time.time()-started,'resources':table,'status':status}
        atomic_json(root/'reports/pilot.json',report);event('verification',**checks)
        if not report['passed']:raise AssertionError(checks)
    finally:
        for t in transports.values():
            try:t.rpc('stop')
            except Exception as error:event('stop_error',host=t.name,error=str(error))
        c.close()

if __name__=='__main__':main()
