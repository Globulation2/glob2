"""Inspect the first divergent tick without loading complete sidecars into RAM."""
import json,pathlib,struct,sys
root=pathlib.Path(sys.argv[1]);output=pathlib.Path(sys.argv[2])
def read_tick(path,wanted):
 with path.open('rb') as f:
  header=f.read(20);teams,players,count,flags=struct.unpack_from('<4I',header,4)
  for _ in range(count):
   tick,aggregate=struct.unpack('<II',f.read(8));record={}
   for team in range(teams):
    checksum=struct.unpack('<I',f.read(4))[0]
    if tick==wanted:record[f'team{team}']=checksum
    for kind in ['unit','building']:
     n=struct.unpack('<I',f.read(4))[0]
     for _ in range(n):
      gid,cs,fields=struct.unpack('<HII',f.read(10))
      if tick==wanted:record[f'team{team}/{kind}{gid}']=[cs,list(struct.unpack('<'+'I'*fields,f.read(4*fields)))]
      else:f.seek(4*fields,1)
   if tick==wanted:return record
 raise ValueError('Tick missing')
report={}
for label,prefix,tick in [('candidate','',24578),('control','control-',24591)]:
 a=read_tick(root/(prefix+'uninterrupted')/'game.replay.checksums',tick);b=read_tick(root/(prefix+'resumed')/'game.replay.checksums',tick)
 report[label]=dict(tick=tick,differences={k:dict(continuous=a.get(k),resumed=b.get(k)) for k in sorted(set(a)|set(b)) if a.get(k)!=b.get(k)})
output.write_text(json.dumps(report,indent=2)+'\n');print({k:dict(tick=v['tick'],entities=list(v['differences'])) for k,v in report.items()})
