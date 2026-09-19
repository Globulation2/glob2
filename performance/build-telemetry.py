from pathlib import Path
import shlex,subprocess
r=Path('artifacts/who-ate-the-map/performance');s=Path('test/MapGeneratorGoldenTest.cpp').read_text();s=s.replace('for (int id : GeneratorRegistry::builtins().methods(true))','for (int id : {GeneratorRegistry::builtins().idOf("who-ate-the-map")})');s=s.replace('request.wDec = request.hDec = 8;','request.wDec = request.hDec = 9;\n\t\trequest.nbTeams = 8;');s=s.replace('seed <= 3','seed <= 9').replace('request.seed = seed;','request.seed = seed;\n\t\t\trequest.options["appetite"] = (seed - 1) % 3;');source=r/'TelemetryCheck.cpp';source.write_text(s)
logs='\n'.join(p.read_text() for p in Path('/tmp').glob('eaten-build*.log'))
compile_cmd=next(shlex.split(l) for l in logs.splitlines() if l.startswith('g++ ') and ' -c ' in l and 'MapGeneratorDefaultsTest.cpp' in l)
obj='build/src/MapGeneratorGoldenTest.o';compile_cmd[compile_cmd.index('-o')+1]=str(r/'TelemetryCheck.o');compile_cmd[-1]=str(source);compile_cmd+=['-Itest'];subprocess.run(compile_cmd,check=True,stdout=subprocess.DEVNULL)
link=next(shlex.split(l) for l in logs.splitlines() if l.startswith('g++ -o build/src/MapGeneratorGoldenTest '));link=[str(r/'telemetry-check') if x=='build/src/MapGeneratorGoldenTest' else str(r/'TelemetryCheck.o') if x==obj else x for x in link];subprocess.run(link,check=True)
