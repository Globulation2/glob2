#!/usr/bin/env python3
"""Persistent single-thread workers; uncertain execution is never retried as a transfer.

Run one local worker per allowed CPU on each host. SSH is used only to submit
or inspect persistent workers, not as an engine lifetime/timeout mechanism.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import time

import maxima_win_experiment as experiment


def affinity(topology,ceiling,allowed=None):
    """lscpu -p=CPU,CORE,SOCKET,ONLINE; reserve a whole physical core."""
    cores={}; seen=set()
    for line in topology.splitlines():
        if not line.strip() or line.startswith('#'): continue
        fields=line.split(',')
        if len(fields)!=4: raise ValueError('require CPU,CORE,SOCKET,ONLINE topology')
        cpu,core,socket=map(int,fields[:3]); online=fields[3].strip().lower()
        if online not in ('y','yes','1'): continue
        if cpu in seen: raise ValueError('duplicate CPU')
        seen.add(cpu); cores.setdefault((socket,core),[]).append(cpu)
    if len(cores)<2: raise ValueError('cannot reserve a physical core')
    reserved=max(cores,key=lambda k:(len(cores[k]),k))
    available=[cpu for core in sorted(cores) if core!=reserved for cpu in sorted(cores[core])
               if allowed is None or cpu in allowed]
    if ceiling<1 or not available: raise ValueError('no permitted simulation capacity')
    return {'reserved_core':list(reserved),'reserved_cpus':cores[reserved],
            'simulation_cpus':available[:ceiling],'jobs':min(ceiling,len(available))}


class Health:
    def __init__(self,failures=0,successes=0,enabled=True):
        self.failures=failures; self.successes=successes; self.enabled=enabled
    def observe(self,success):
        if success:
            self.successes+=1; self.failures=0
            if self.successes>=3: self.enabled=True
        else:
            self.failures+=1; self.successes=0
            if self.failures>=3: self.enabled=False
        return self.enabled


class Queue:
    def __init__(self,path):
        self.db=sqlite3.connect(path,timeout=30,isolation_level=None)
        self.db.row_factory=sqlite3.Row
        self.db.execute('PRAGMA journal_mode=WAL'); self.db.execute('PRAGMA synchronous=FULL')
        self.db.executescript('''
        CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY,payload TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'pending',host TEXT,cpu INTEGER,pid INTEGER,
          result TEXT,error TEXT,started REAL,finished REAL);
        CREATE TABLE IF NOT EXISTS transfers(id TEXT PRIMARY KEY,status TEXT NOT NULL DEFAULT 'pending',
          attempts INTEGER NOT NULL DEFAULT 0,error TEXT);
        CREATE TABLE IF NOT EXISTS hosts(host TEXT PRIMARY KEY,checked REAL,failures INTEGER,
          successes INTEGER,enabled INTEGER,topology TEXT);
        ''')
    def close(self):
        self.db.close()
    def enqueue(self,payload):
        key=payload.get('execution_id',experiment.identity(payload))
        self.db.execute('INSERT OR IGNORE INTO jobs(id,payload) VALUES(?,?)',(key,experiment.canonical(payload)))
        return key
    def claim(self,host,cpu):
        self.db.execute('BEGIN IMMEDIATE')
        try:
            health=self.db.execute('SELECT enabled,checked FROM hosts WHERE host=?',(host,)).fetchone()
            # Stale or unknown health is not authority to dispatch.
            if health is None or not health['enabled'] or time.time()-health['checked']>90:
                self.db.execute('COMMIT'); return None
            row=self.db.execute("SELECT * FROM jobs WHERE status='pending' ORDER BY rowid LIMIT 1").fetchone()
            if row:
                self.db.execute("UPDATE jobs SET status='running',host=?,cpu=?,pid=?,started=? WHERE id=?",
                                (host,cpu,os.getpid(),time.time(),row['id']))
            self.db.execute('COMMIT'); return dict(row) if row else None
        except BaseException:
            self.db.execute('ROLLBACK'); raise
    def finish(self,key,result):
        self.db.execute("UPDATE jobs SET status='complete',result=?,finished=? WHERE id=?",
                        (experiment.canonical(result),time.time(),key))
        self.db.execute('INSERT OR IGNORE INTO transfers(id) VALUES(?)',(key,))
    def fail(self,key,error):
        self.db.execute("UPDATE jobs SET status='needs_investigation',error=?,finished=? WHERE id=?",
                        (str(error),time.time(),key))
    def health(self,host,success,topology=None):
        row=self.db.execute('SELECT * FROM hosts WHERE host=?',(host,)).fetchone()
        h=Health(row['failures'],row['successes'],bool(row['enabled'])) if row else Health(enabled=False)
        h.observe(success)
        frozen_topology=experiment.canonical(topology) if topology is not None else row['topology'] if row else None
        self.db.execute('INSERT OR REPLACE INTO hosts VALUES(?,?,?,?,?,?)',
                        (host,time.time(),h.failures,h.successes,int(h.enabled),frozen_topology))
    def transfer(self,key,success,error=None):
        self.db.execute('UPDATE transfers SET status=?,attempts=attempts+1,error=? WHERE id=?',
                        ('complete' if success else 'pending',error,key))
    def summary(self):
        counts=dict(self.db.execute('SELECT status,count(*) FROM jobs GROUP BY status').fetchall())
        counts['pending_transfers']=self.db.execute("SELECT count(*) FROM transfers WHERE status!='complete'").fetchone()[0]
        return counts


def run_job(protocol,payload,directory,cpu):
    directory.mkdir(parents=True,exist_ok=False)
    scenario=payload['scenario']; settings=payload['settings']
    checkpoint=None; receipts=[]; started=time.monotonic()
    continuous=protocol.get('execution_mode')=='uninterrupted'
    caps=(experiment.CHECKPOINTS[-1],) if continuous else experiment.CHECKPOINTS
    for cap in caps:
        audit=directory/f'{cap}.audit.jsonl'; save=directory/f'{cap}.game'
        args,envelope=experiment.command(protocol,scenario,settings,cap,audit,checkpoint,save)
        if continuous:
            harvest=directory/'checkpoints'; harvest.mkdir()
            args+=['--maxima-checkpoint-harvest',str(harvest.resolve()),str(protocol['checkpoint_harvest_interval'])]
            save=harvest/f'checkpoint-{cap}.game'
        if not hasattr(os,'sched_getaffinity') or cpu not in os.sched_getaffinity(0):
            raise experiment.IntegrityError('worker CPU not in verified Linux affinity')
        args=['taskset','-c',str(cpu),*args]
        experiment.atomic(directory/f'{cap}.command.json',{'argv':args,'identities':envelope})
        with (directory/f'{cap}.engine.log').open('w') as log:
            child=subprocess.Popen(args,cwd=experiment.ROOT,env=experiment.clean_environment(),stdout=log,stderr=subprocess.STDOUT)
            experiment.atomic(directory/'engine.json',{'pid':child.pid,'cap':cap,'status':'running'})
            # No engine timeout and no killing an SSH client. Job persists if controller vanishes.
            code=child.wait()
        experiment.atomic(directory/'engine.json',{'pid':child.pid,'cap':cap,'status':'exited','exit_code':code})
        if code: raise experiment.IntegrityError(f'engine exited {code}; inspect before retry')
        receipt=experiment.read_audit(audit,envelope,scenario,settings)
        if checkpoint and experiment.deterministic_signature(receipt['start'])!=experiment.deterministic_signature(receipts[-1]['terminal']):
            raise experiment.IntegrityError('save/load continuation state differs; stop fleet dispatch')
        receipts.append(receipt)
        experiment.atomic(directory/'receipts.json',receipts)
        if receipt['outcome'] is not None: break
        if receipt['terminal']['tick']!=cap or not save.is_file():
            raise experiment.IntegrityError('unfinished run missing scheduled checkpoint')
        checkpoint=(save,cap)
    result={'scenario_id':scenario['scenario_id'],'execution_id':payload.get('execution_id'),
            'repeat':payload.get('repeat',False),'configuration_id':experiment.identity(settings),
            'execution_mode':protocol.get('execution_mode','staged'),
            **experiment.adjudicate(protocol,scenario,receipts[-1]),'seconds':time.monotonic()-started,
            'tick':receipts[-1]['terminal']['tick'],'receipts':receipts}
    experiment.atomic(directory/'result.json',result)
    return result


def worker(out,host,cpu):
    protocol=json.loads((out/'protocol.json').read_text()); experiment.verify_freeze(protocol)
    queue=Queue(out/'queue.sqlite')
    health=queue.db.execute('SELECT topology FROM hosts WHERE host=?',(host,)).fetchone()
    if health is None or not health['topology'] or cpu not in json.loads(health['topology'])['simulation_cpus']:
        raise experiment.IntegrityError('CPU is not in verified host topology allocation')
    while not (out/'STOP_DISPATCH.json').exists() and not (out/'RETIRED.json').exists():
        import shutil
        if shutil.disk_usage(out).free < 5 * 1024**3:
            experiment.atomic(out/'STOP_DISPATCH.json',{'reason':'less than 5 GiB free storage',
                'host':host,'active_engines':'preserve'})
            break
        row=queue.claim(host,cpu)
        if not row: time.sleep(5); continue
        payload=json.loads(row['payload'])
        try:
            experiment.require_gates(out,protocol,payload['scenario']['stage'])
            result=run_job(protocol,payload,out/'runs'/row['id'],cpu)
            queue.finish(row['id'],result)
        except Exception as error:
            queue.fail(row['id'],error)
            experiment.atomic(out/'STOP_DISPATCH.json',{'reason':str(error),'job':row['id'],'host':host,
                              'active_engines':'preserve; stop new assignments'})
            raise


def health_monitor(out):
    queue=Queue(out/'queue.sqlite'); last_report=0
    while not (out/'RETIRED.json').exists():
        cycle_started=time.monotonic()
        # Independent probes run concurrently so a failed host cannot defer
        # another host's minute check. SQLite writes stay on this thread.
        def probe(item):
            host,ceiling=item
            try:
                # This SSH client runs only a topology probe, never an engine.
                probe=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,
                    'lscpu -p=CPU,CORE,SOCKET,ONLINE'],capture_output=True,text=True,timeout=12)
                topology=affinity(probe.stdout,ceiling) if probe.returncode==0 else None
                return host,topology
            except (subprocess.TimeoutExpired,ValueError,OSError): return host,None
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=5) as executor:
            for host,topology in executor.map(probe,experiment.HOSTS.items()):
                queue.health(host,topology is not None,topology)
        if time.time()-last_report>=3600:
            import shutil
            experiment.atomic(out/'fleet-status.json',{'time':time.time(),'queue':queue.summary(),
                'free_bytes':shutil.disk_usage(out).free,'eta':'requires measured completed pairs and allocation'})
            last_report=time.time()
        time.sleep(max(0,60-(time.monotonic()-cycle_started)))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True); parser.add_argument('--host')
    parser.add_argument('--cpu',type=int); parser.add_argument('--monitor',action='store_true')
    args=parser.parse_args()
    if args.monitor: health_monitor(args.output)
    elif args.host is not None and args.cpu is not None: worker(args.output,args.host,args.cpu)
    else: parser.error('choose --monitor or --host and --cpu')
