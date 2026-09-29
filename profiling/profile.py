import pathlib,subprocess,shlex,os,time,json,collections
root=pathlib.Path.cwd();e=root/'artifacts/broad-pass';e.mkdir(exist_ok=True)
binary=e/'baseline-symbols'
if not binary.exists():
 line=next(x for x in (root/'artifacts/pr-final/build-candidate.log').read_text().splitlines() if x.startswith('g++ -o build/linux/client/release/src/glob2 '));cmd=[x for x in shlex.split(line) if x!='-s'];cmd[2]=str(binary);subprocess.run(cmd,check=True)
cases=json.loads((root/'artifacts/pr-final/evidence/cases.json').read_text());merged=collections.Counter();profiles={}
for name in ['even12','held-islands12-late','held-even12-late','held-islands8','held-continents12']:
 c=next(c for c in cases if c['id']==name);out=e/('profile-'+name);out.mkdir();fifo=out/'control';os.mkfifo(fifo);fd=os.open(fifo,os.O_RDWR|os.O_NONBLOCK)
 cmd=['sudo','-n','perf','record','--delay=-1','--control=fifo:'+str(fifo),'-e','cycles:u','-F','99','--call-graph','dwarf,8192','-o',str(out/'perf.data'),'--','stdbuf','-oL',str(binary),'--run-game','--load-game',str(root/'artifacts/pr-final/evidence'/c['save']),'--ticks',str(c['tick']+2000),'--gradient-workers','1','--gradient-delay','8','--output-dir',str(out)]
 (out/'command.json').write_text(json.dumps(cmd));start=time.monotonic()
 with (out/'stdout.log').open('w') as log,(out/'perf.log').open('w') as err:
  proc=subprocess.Popen(cmd,stdout=log,stderr=err);enabled=False
  while proc.poll() is None:
   if not enabled and 'nox::game started' in (out/'stdout.log').read_text():os.write(fd,b'enable\n');enabled=True;(out/'start.json').write_text(json.dumps({'enable_after_seconds':time.monotonic()-start}))
   time.sleep(.01)
  assert proc.returncode==0 and enabled,name
 os.close(fd);fifo.unlink()
 report=subprocess.check_output(['sudo','-n','perf','report','--stdio','--no-children','--show-total-period','--field-separator=;','--percent-limit=0','-g','none','-i',str(out/'perf.data')],text=True);(out/'flat.csv').write_text(report);counts=collections.Counter()
 for line in report.splitlines():
  if line.startswith('#'):continue
  parts=line.split(';',4)
  if len(parts)==5:counts[parts[4].strip()]+=int(parts[1])
 profiles[name]={'total_period':sum(counts.values()),'symbols':dict(counts)};merged.update(counts)
 with (out/'callers.txt').open('w') as f:subprocess.run(['sudo','-n','perf','report','--stdio','--children','--percent-limit','0.5','-g','graph,0.5,caller','-i',str(out/'perf.data')],stdout=f,check=True)
 total=sum(merged.values());(e/'profiles.json').write_text(json.dumps({'profiles':profiles,'pooled':[{'symbol':s,'percent':100*v/total,'equal_game_percent':sum(100*r['symbols'].get(s,0)/r['total_period'] for r in profiles.values())/len(profiles)} for s,v in merged.most_common()]},indent=2));print(name,'DONE',flush=True)
