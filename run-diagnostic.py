from pathlib import Path
import json,subprocess,sys
R=Path('/home/bradley/glob2-maxima-final');D=R/'artifacts/echo-save-continuity';B=D/'glob2-diagnostic';old=R/'artifacts/final-maxima/order-diagnostic'
for name in ('full','resumed'):
 cmd=json.loads((old/name/'command.json').read_text());cmd[0]=str(B);cmd[cmd.index('--output-dir')+1]=str(D/name)
 if name=='resumed':cmd[cmd.index('--load-game')+1]=str(D/'full/checkpoint-4096.game')
 out=D/name;out.mkdir(exist_ok=True);(out/'command.json').write_text(json.dumps(cmd))
 with (out/'run.log').open('w') as f:subprocess.run(cmd,cwd=R,stdout=f,stderr=f,check=True)
 sys.path.insert(0,str(R/'test'));from compare_save_continuation import compare
 print(name,compare(old/name/'game.replay.checksums',out/'game.replay.checksums'),flush=True)
