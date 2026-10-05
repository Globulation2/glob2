import pathlib,shlex,subprocess,json
base=pathlib.Path.cwd();out=base/'artifacts/replay-import-telemetry';lines=(out/'final-build-verified.log').read_text().splitlines()
compile=next(shlex.split(x) for x in lines if x.startswith('g++ ') and '-c ' in x and x.endswith('test/SavegameSafetyHarness.cpp'))
compile[compile.index('-o')+1]=str(out/'fixture-integration.o');compile[-1]=str(out/'fixture-integration.cpp')
link=next(shlex.split(x) for x in lines if x.startswith('g++ -o build-software-terrain/test/glob2-engine-tests '));link[link.index('-o')+1]=str(out/'fixture-integration-tests');link.insert(link.index('-Lbuild/sdl3-ci/prefix/lib'),str(out/'fixture-integration.o'))
(out/'integration-commands.json').write_text(json.dumps([compile,link],indent=2)+'\n')
subprocess.run(compile,check=True);subprocess.run(link,check=True)
