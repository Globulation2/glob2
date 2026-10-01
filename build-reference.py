from pathlib import Path
import shlex, subprocess
p = Path('artifacts/render-profile')
commands = [shlex.split(line) for line in (p/'cpu-build.log').read_text().splitlines() if line.startswith('g++ ')]
compilecmd = next(c for c in commands if '-c' in c and c[-1] == 'test/TorusRenderBenchmark.cpp')
for name in ('GameRender', 'GameRenderTerrain'):
    source = p/('reference-'+name+'.cpp')
    source.write_bytes(subprocess.check_output(['git', 'show', '777d19e09:src/render/'+name+'.cpp']))
    cmd = compilecmd.copy()
    cmd[cmd.index('-o')+1] = str(p/('reference-'+name+'.o'))
    cmd[-1] = str(source)
    subprocess.run(cmd, check=True)
cmd = next(c for c in commands if '-c' not in c and 'build/darwin/client/release/test/torus-render-benchmark' in c)
cmd[cmd.index('-o')+1] = str(p/'ai-reference-benchmark')
for name in ('GameRender', 'GameRenderTerrain'):
    cmd[cmd.index('build/darwin/client/release/src/render/'+name+'.o')] = str(p/('reference-'+name+'.o'))
subprocess.run(cmd, check=True)
