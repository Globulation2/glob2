import json, pathlib, shlex, subprocess
root = pathlib.Path('artifacts/ci-repair')
# Replay the old generation/options body with the current registry metadata.
# Pre-#674 controls lack the now-required explicit search domains, so their old
# definition cannot be registered unchanged by the current engine. The fixture
# forces pattern=0 and barrier=0; all other numeric defaults remain historical.
old = subprocess.check_output(['git', 'show', 'e674c61e3^:src/map/generator/generators/FingerprintGenerator.cpp'], text=True)
current = pathlib.Path('src/map/generator/generators/FingerprintGenerator.cpp').read_text()
marker = 'GeneratorDefinition fingerprintDefinition()'
(root / 'FingerprintGenerator-pre-random.cpp').write_text(old[:old.index(marker)] + current[current.index(marker):])
lines = (root / 'map-coverage-native-build.log').read_text().splitlines()
commands = []
for line in lines:
    if line.startswith('clang++-18 ') and line.endswith('src/map/generator/generators/FingerprintGenerator.cpp'):
        args = shlex.split(line)
        args[args.index('-o')+1] = str(root / 'FingerprintGenerator-pre-random.o')
        args[-1] = str(root / 'FingerprintGenerator-pre-random.cpp')
        commands.append(args)
        subprocess.run(args, check=True)
        break
else:
    raise SystemExit('Missing Fingerprint compile command')
out = root / 'coverage-pre-random/test'
out.mkdir(parents=True, exist_ok=True)
for line in lines:
    if line.startswith('clang++-18 -o build/native-coverage-repair/test/glob2-engine-tests '):
        args = shlex.split(line)
        args[2] = str(out / 'glob2-engine-tests')
        old = 'build/native-coverage-repair/src/map/generator/generators/FingerprintGenerator.o'
        assert args.count(old) == 1
        args[args.index(old)] = str(root / 'FingerprintGenerator-pre-random.o')
        commands.append(args)
        subprocess.run(args, check=True)
        break
else:
    raise SystemExit('Missing engine link command')
(root / 'coverage-pre-random-commands.json').write_text(json.dumps(commands, indent=2)+'\n')
