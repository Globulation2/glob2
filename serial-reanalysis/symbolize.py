from pathlib import Path
import subprocess,time,json
out=Path.cwd()/'artifacts/serial-reanalysis'
while not (out/'run.exit').exists():time.sleep(2)
assert (out/'run.exit').read_text().strip()=='0'
for row in json.loads((out/'results.json').read_text()):
 if row['mode']!='perf':continue
 p=out/f"{row['fixture']}-{row['participants']}-perf"
 with (p/'report.txt').open('w') as f, (p/'symbolize.log').open('w') as e:
  subprocess.run(['sudo','-n','perf','report','-f','--stdio','--no-children','--sort','pid,symbol','--call-graph','none','-i',str(p/'perf.data')],stdout=f,stderr=e,check=True)
 print('Symbolized',p.name,flush=True)
subprocess.run(['python3',str(out/'analyze.py')],check=True)
