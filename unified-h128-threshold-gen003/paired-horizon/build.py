import datetime, fcntl, hashlib, json, os, pathlib, shlex, subprocess, traceback

B = pathlib.Path(__file__).resolve().parent
O = B / 'build'
O.mkdir(exist_ok=True)
S = O / 'source'
OLD = pathlib.Path('/home/bradley/glob2-gradient-unified-h128')
REV = '4a2cf3dac9eb4eab0aab932307ca5096be953491'
ELF = '5f6616edf50ac7ccfd3878edd70cd4b85719731a8a73453d716ab7d53303b848'
guard = (O / 'lock').open('a')
fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
sha = lambda p: hashlib.file_digest(pathlib.Path(p).open('rb'), 'sha256').hexdigest()
state = dict(status='building-research-only', source_base=REV, frozen_base_elf=ELF, pid=os.getpid(),
             started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save(): (O / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
def run(argv, **kwargs): return subprocess.run(argv, check=True, **kwargs)
save()
try:
    assert subprocess.check_output(['git', '-C', str(OLD), 'rev-parse', 'HEAD'], text=True).strip() == REV
    assert not subprocess.check_output(['git', '-C', str(OLD), 'status', '--porcelain'], text=True).strip()
    assert sha(OLD / 'build/unified-release/src/glob2') == ELF
    run(['git', 'clone', '--shared', '--no-checkout', str(OLD), str(S)], stdout=subprocess.DEVNULL)
    run(['git', '-C', str(S), 'checkout', '--detach', REV], stdout=subprocess.DEVNULL)
    os.symlink(OLD / 'build', S / 'build', target_is_directory=True)  # Read-only generated inputs.
    run(['python3', str(B/'patch.py'),str(S)])
    changes = ['src/map/gradient/ResourceGradientProbe.cpp']
    assert set(subprocess.check_output(['git', '-C', str(S), 'diff', '--name-only'], text=True).splitlines()) == set(changes)
    (O / 'paired-horizon.patch').write_bytes(subprocess.check_output(['git', '-C', str(S), 'diff', '--', *changes]))
    logs = [OLD.parent / 'glob2/artifacts/gradient-unified-20261019/builds/linux/build-f64.log', OLD.parent / 'glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/linux/build.log']
    lines = [line for log in logs for line in log.read_text().splitlines()]
    replacements, commands = {}, []
    for source in changes:
        target = 'build/unified-release/' + str(pathlib.Path(source).with_suffix('.o'))
        original = shlex.split(next(line for line in reversed(lines) if ' -o ' + target + ' ' in line and ' -c ' in line))
        argv = list(original)
        obj = O / (pathlib.Path(source).stem + '.o')
        argv[argv.index('-o')+1] = str(obj)
        assert argv[-1] == source
        argv[-1] = str(S / source)
        replacements[target] = str(obj)
        commands.append(dict(original_argv=original, argv=argv, cwd=str(S), source_sha256=sha(S / source), old_object_sha256=sha(OLD / target)))
        (O / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (O / (pathlib.Path(source).stem + '.log')).open('w') as log:
            run(['taskset', '-c', '4', 'nice', '-n', '19', *argv], cwd=S, stdout=log, stderr=subprocess.STDOUT)
    original_link = next(shlex.split(line) for line in reversed(lines) if line.startswith('g++ -o build/unified-release/src/glob2 '))
    link = [replacements.get(v, v) for v in original_link]
    binary = O / 'glob2'
    link[link.index('-o')+1] = str(binary)
    reused = {v: sha(OLD / v) for v in link if v.endswith(('.o', '.a')) and not pathlib.Path(v).is_absolute()}
    (O / 'reused-link-inputs.json').write_text(json.dumps(reused, indent=2) + '\n')
    (O / 'link-command.json').write_text(json.dumps(dict(original_argv=original_link, argv=link, cwd=str(OLD)), indent=2) + '\n')
    with (O / 'link.log').open('w') as log:
        run(['taskset', '-c', '4', 'nice', '-n', '19', *link], cwd=OLD, stdout=log, stderr=subprocess.STDOUT)
    assert all(sha(OLD / p) == h for p, h in reused.items())
    assert sha(OLD / 'build/unified-release/src/glob2') == ELF
    assert not subprocess.check_output(['git', '-C', str(OLD), 'status', '--porcelain'], text=True).strip()
    state.update(status='built-research-only', binary=str(binary), binary_sha256=sha(binary),
                 patch_sha256=sha(O / 'paired-horizon.patch'), reused_inputs=len(reused),
                 model_sha256=sha(OLD / 'tools/models/adaptive-gradient.json'),
                 header_sha256=sha(OLD / 'src/map/gradient/AdaptiveGradientModel.h'),
                 finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    save()
except BaseException:
    state.update(status='failed', error=traceback.format_exc())
    save()
    raise
