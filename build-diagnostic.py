from pathlib import Path
import shlex,subprocess
R=Path('/home/bradley/glob2-maxima-final');D=R/'artifacts/echo-save-continuity';lines=Path('/home/bradley/glob2-maxima-final-build.log').read_text().splitlines();objects={}
for name in ('Echo','Gradient'):
 source='src/ai/echo/'+name+'.cpp';line=next(l for l in lines if l.startswith('g++ ') and l.endswith(' '+source));cmd=shlex.split(line);old=cmd[cmd.index('-o')+1];obj=str(D/(name+'.o'));cmd[cmd.index('-o')+1]=obj;cmd[-1]=str(D/(name+'.cpp'));subprocess.run(cmd,cwd=R,check=True);objects[old]=obj
cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o build/linux/client/release/src/glob2 ')));cmd=[objects.get(x,x) for x in cmd];cmd[cmd.index('-o')+1]=str(D/'glob2-diagnostic');subprocess.run(cmd,cwd=R,check=True)
