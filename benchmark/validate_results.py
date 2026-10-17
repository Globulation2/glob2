from pathlib import Path
import array,datetime,gzip,hashlib,json,platform,struct,subprocess,sys,xml.etree.ElementTree as ET
root=Path.cwd();out=root/'artifacts/hiring-prewarm-top-candidates';exp=json.loads((out/'experiment.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(Path(exp['binary']))==exp['binary_sha256']
assert sha(out/'source-sha256.json')==exp['source_manifest_sha256']
source=json.loads((out/'source-sha256.json').read_text())
for path,expected in source.items():assert sha(root/path)==sha(out/'source'/path)==expected,path
cases=[];pairs=[];maps=[]
for w in exp['workloads']:
 mapfile=root/'maps'/w['map'];assert sha(mapfile)==w['map_sha256']
 b=gzip.open(mapfile,'rb').read();length=struct.unpack_from('>I',b)[0];major,minor,teams,offset=struct.unpack_from('>4I',b,4+length);assert b[offset:offset+4]==b'MapB';lw,lh=struct.unpack_from('>2I',b,offset+4)
 maps.append(dict(name=w['name'],file=w['map'],width=1<<lw,height=1<<lh,teams=teams,sha256=w['map_sha256'],players=w['players'],tick_limit=w['ticks']))
 assert teams==len(w['players'])
 for seed in exp['seeds']:
  values={}
  for mode in exp['policies']:
   for n in exp['participants']:
    path=out/w['name']/f'seed-{seed}-{mode}-p{n}';m=json.loads((path/'run.json').read_text());v=json.loads((path/'game/result.json').read_text());cmd=m['command']
    assert m['exit_code']==0 and m['host_contention_seen'] is False,path
    assert m['binary_sha256']==exp['binary_sha256']
    assert cmd[cmd.index('--ticks')+1]==str(w['ticks']) and cmd[cmd.index('--game-seed')+1]==str(seed)
    assert cmd[cmd.index('--compute-threads')+1]==str(n) and m['environment']['GLOB2_HIRING_PREWARM']==mode
    assert '--telemetry' not in cmd # expensive per-tick checksum tracing belongs to smoke checks only
    samples=array.array('Q');samples.frombytes((path/'tick-ns.u64').read_bytes());assert len(samples)==v['benchmark_measured_ticks']>0
    assert v['hiring_gradient_probe']['mode']=={'baseline':0,'shortlist':1,'top':3}[mode]
    if mode=='top':assert v['hiring_gradient_probe']['warmed_extra']==0
    values[n,mode]=v
    cases.append(dict(case=str(path.relative_to(out)),ticks=len(samples),initial_checksum=v['initialChecksum'],final_checksum=v['finalChecksum'],result_sha256=sha(path/'game/result.json'),sample_sha256=sha(path/'tick-ns.u64')))
   assert values[1,mode]['finalChecksum']==values[4,mode]['finalChecksum']
   pairs.append(dict(workload=w['name'],seed=seed,mode=mode,final_checksum=values[1,mode]['finalChecksum']))
  assert len({v['initialChecksum'] for v in values.values()})==1
assert len(cases)==384 and len(pairs)==192
smoke=[]
for mode in exp['policies']:
 a=out/'smoke/holiday'/f'seed-42-{mode}-p1/game';b=out/'smoke/holiday'/f'seed-42-{mode}-p4/game'
 hashes={}
 for file in ['world.checksums','game.replay.checksums']:
  assert (a/file).read_bytes()==(b/file).read_bytes();hashes[file]=sha(a/file)
 smoke.append(dict(mode=mode,identical_trace_sha256=hashes))
reports=[]
for name in ['baseline-focused','shortlist-handoff','top-handoff']:
 xml=ET.parse(out/(name+'-junit.xml')).getroot();assert int(xml.attrib.get('failures',0))==int(xml.attrib.get('errors',0))==0;reports.append(dict(report=name+'-junit.xml',**xml.attrib))
result=dict(validated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),base=exp['base_revision'],binary_sha256=exp['binary_sha256'],source_manifest_sha256=exp['source_manifest_sha256'],platform=platform.platform(),architecture=platform.machine(),compiler=subprocess.check_output(['clang','--version'],text=True),build_command=exp['build_command'],flags='release=1, -O3 -g, client_profile=full, server=0, no PCH/unity; all policies use the same executable',dependency_manifest_sha256=sha(root/'vcpkg.json'),maps=maps,valid_timed_runs=len(cases),final_checksum_pairs=len(pairs),total_executed_ticks=sum(c['ticks'] for c in cases),cases=cases,pairs=pairs,smoke=smoke,focused_reports=reports)
(out/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:result[k] for k in ['valid_timed_runs','final_checksum_pairs','total_executed_ticks','binary_sha256','maps','focused_reports']},indent=2))
