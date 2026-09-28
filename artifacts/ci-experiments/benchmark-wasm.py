import json, os, pathlib, subprocess, time, sys, shutil
root = pathlib.Path(__file__).resolve().parents[2]
output = root / 'build/emscripten/client/release'
evidence = root / 'artifacts/ci-experiments'
label = sys.argv[1]
assert label in ('uncached', 'warm', 'warm-fresh')
# Remove only generated files owned by this isolated experiment.
# warm-fresh also rebuilds ports to model a fresh hosted runner.
for path in (output / 'obj').rglob('*.o'):
    path.unlink()
for suffix in ('html', 'js', 'wasm', 'data'):
    (output / ('index.' + suffix)).unlink(missing_ok=True)
if label == 'warm-fresh':
    for name in ('cache', 'ports', 'tmp'):
        shutil.rmtree(output / name, ignore_errors=True)
    (output / 'ports-ready.o').unlink(missing_ok=True)
env = dict(os.environ, EM_COMPILER_WRAPPER='/opt/homebrew/bin/ccache',
           CCACHE_DIR=str(evidence / 'wasm-cache'),
           CCACHE_COMPILERCHECK='content', CCACHE_MAXSIZE='500M')
if label == 'uncached':
    env['CCACHE_DISABLE'] = '1'
else:
    env.pop('CCACHE_DISABLE', None)
subprocess.run(['ccache', '-z'], env=env, check=True, stdout=subprocess.DEVNULL)
command = ['scons', 'target=web', 'release=1',
           'emsdk=/path/to/sdk-checkout/tools/browser-emsdk', '-j4']
start = time.monotonic()
with (evidence / ('wasm-' + label + '.log')).open('w') as log:
    result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
elapsed = time.monotonic() - start
stats = subprocess.check_output(['ccache', '--print-stats'], env=env, text=True)
(evidence / ('wasm-' + label + '-stats.txt')).write_text(stats)
report = dict(command=command, seconds=elapsed, returncode=result.returncode,
              ports='rebuilt' if label == 'warm-fresh' else 'already built', jobs=4)
(evidence / ('wasm-' + label + '.json')).write_text(json.dumps(report, indent=2))
print(report, flush=True)
sys.exit(result.returncode)
