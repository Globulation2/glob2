import pathlib,subprocess,shlex,json
r=pathlib.Path.cwd();e=r/'artifacts/broad-pass/scan-visits';e.mkdir(exist_ok=True);objects={}
log=(r/'artifacts/broad-pass/build-macos-tests.log').read_text()
for file,field,query,label in [('CortexObservationObserve.cpp','obs.fruitOnMap','map.isResourceTakeable(x,y,CHERRY) || map.isResourceTakeable(x,y,ORANGE) || map.isResourceTakeable(x,y,PRUNE)','fruit'),('CortexWater.cpp','out.algaeDiscovered','map.isResourceTakeable(x,y,ALGA) && map.isMapDiscovered(x,y,team->allies)','algae')]:
 original=r/'src/ai/cortex'/file;s=original.read_text();start=s.index('for (int y = 0; y < h && '+field);end=s.index('\n\n',start)
 code='''auto scan = [&](bool rowMajor) {
 bool found=false; unsigned long long visits=0;
 for(int a=0;a<(rowMajor?h:w) && !found;++a)
  for(int b=0;b<(rowMajor?w:h);++b) {
   const int x=rowMajor?b:a, y=rowMajor?a:b; ++visits;
   if(QUERY) {found=true;break;}
  }
 return std::make_pair(found,visits);
};
const auto row=scan(true),column=scan(false);
if(row.first!=column.first)std::abort();
FIELD=row.first;
std::fprintf(stderr,"SCAN_LABEL %llu %llu\\n",row.second,column.second);'''.replace('QUERY',query).replace('FIELD',field).replace('LABEL',label)
 s='#include <cstdio>\n#include <cstdlib>\n#include <utility>\n'+s[:start]+code+s[end:];src=e/file;src.write_text(s)
 line=next(x for x in log.splitlines() if x.startswith('g++ -o ') and x.endswith('src/ai/cortex/'+file));cmd=shlex.split(line);old=cmd[2];obj=e/(file+'.o');cmd[2]=str(obj);cmd[-1]=str(src);cmd+=['-Isrc/ai/cortex'];subprocess.run(cmd,check=True);objects[old]=str(obj)
linklog=(r/'artifacts/broad-pass/build-macos-final.log').read_text();line=next(x for x in linklog.splitlines() if x.startswith('g++ -o build/darwin/client/release/src/glob2 '));cmd=[objects.get(x,x) for x in shlex.split(line)];cmd[2]=str(e/'glob2-scan-visits');subprocess.run(cmd,check=True)
cases=json.loads((r/'artifacts/broad-pass/evidence/cases.json').read_text());summary={}
for name in ['even12','held-islands12-late']:
 c=next(x for x in cases if x['id']==name);out=e/name;out.mkdir();cmd=[str(e/'glob2-scan-visits'),'--run-game','--load-game',str(r/'artifacts/broad-pass/evidence'/c['save']),'--ticks',str(c['tick']+1000),'--output-dir',str(out)];(out/'command.json').write_text(json.dumps(cmd))
 with (out/'stdout.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 totals={}
 for line in (out/'stdout.log').read_text().splitlines():
  if line.startswith('SCAN_'):
   key,row,col=line.split();v=totals.setdefault(key,{'calls':0,'row_visits':0,'column_visits':0});v['calls']+=1;v['row_visits']+=int(row);v['column_visits']+=int(col)
 summary[name]=totals;print(name,totals,flush=True);(e/'results.json').write_text(json.dumps(summary,indent=2))
