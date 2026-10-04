import gzip,json,pathlib,struct,sys,hashlib,itertools

def rows(path):
 with gzip.open(path,'rb') as f:
  header=f.read(20);assert header[:4]==b'GCS1'
  teams,players,count,flags=struct.unpack('<4I',header[4:])
  for _ in range(count):
   parts=[f.read(8)]
   tick,checksum=struct.unpack('<2I',parts[0])
   for team in range(teams):
    parts.append(f.read(4))
    for kind in range(2):
     nbytes=f.read(4);parts.append(nbytes)
     for i in range(struct.unpack('<I',nbytes)[0]):
      row=f.read(10);parts.append(row);parts.append(f.read(struct.unpack('<HII',row)[2]*4))
   yield tick,b''.join(parts)
results=[]
for path in sorted(pathlib.Path(sys.argv[1]).glob('standard-*/*.checksums.gz')):
 name=path.parent.name;candidate=pathlib.Path(sys.argv[2])/name/path.name
 if not candidate.exists():continue
 matched=0;difference=None
 for old,new in itertools.zip_longest(rows(path),rows(candidate)):
  if old!=new:
   difference={'old_tick':old[0] if old else None,'new_tick':new[0] if new else None,'old_checksum':old[1][4:8].hex() if old else None,'new_checksum':new[1][4:8].hex() if new else None};break
  matched+=1
 results.append({'game':name,'matched_ticks':matched,'difference':difference})
 print(results[-1],flush=True)
pathlib.Path(sys.argv[3]).write_text(json.dumps(results,indent=2))
