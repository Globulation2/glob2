import json, os, pathlib, subprocess, time
root = pathlib.Path.cwd()
out = root / 'artifacts/area-effects'
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           GLOB2_USER_DATA_DIR=str(out / 'benchmark-profile'))
binary = str(root / 'build/linux/client/release/test/glob2-engine-tests')
records = []
for trial in range(1, 6):
    modes = ['disabled', 'enabled'] if trial % 2 else ['enabled', 'disabled']
    for mode in modes:
        command = ['taskset', '-c', '24', binary, '--no-breaks=true',
                   '--test-suite=BuildingAreaEffectsBenchmark', '--test-case=populated*']
        started = time.time()
        with (out / f'review-ticks-{mode}-{trial}.log').open('w') as stream:
            result = subprocess.run(command, env=dict(env, GLOB2_TEST_AREA_BENCH_MODE=mode),
                                    stdout=stream, stderr=subprocess.STDOUT)
        records.append(dict(trial=trial, mode=mode, command=command, started=started,
                            wall_seconds=time.time()-started, exit_code=result.returncode,
                            load=os.getloadavg()))
        (out / 'review-benchmark-runs.json').write_text(json.dumps(records, indent=2)+'\n')
        if result.returncode:
            raise SystemExit(result.returncode)
command = ['taskset', '-c', '24', binary, '--no-breaks=true',
           '--test-suite=BuildingAreaEffectsBenchmark', '--test-case=dense fields*']
with (out / 'review-fields.log').open('w') as stream:
    result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
raise SystemExit(result.returncode)
