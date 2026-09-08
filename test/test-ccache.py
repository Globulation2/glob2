#!/usr/bin/env python3
"""Exercise the SCons wrapper with a real cold cache, warm cache and header edit."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('glob2_ccache', root / 'scons/ccache.py')
wrapper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wrapper)

# Respect an explicitly wrapped compiler and never double-wrap commands.
env = {'CC': 'cc', 'CXX': 'ccache c++', 'CCCOM': '$CC -c $SOURCE',
       'CXXCOM': '$CXX -c $SOURCE', 'LINKCOM': '$CXX -o $TARGET $SOURCES', 'ENV': {}}
with patch.object(wrapper.shutil, 'which', return_value='/cache/ccache'):
    wrapper.enable(env)
    wrapper.enable(env)
assert env['CCCOM'] == '/cache/ccache $CC -c $SOURCE'
assert env['CXXCOM'] == '$CXX -c $SOURCE'
assert env['LINKCOM'] == '$CXX -o $TARGET $SOURCES'
with patch.object(wrapper.shutil, 'which', return_value=None):
    try:
        wrapper.enable(env)
    except SystemExit as error:
        assert 'not found' in str(error)
    else:
        raise AssertionError('Missing ccache did not fail')

ccache = shutil.which('ccache')
assert ccache, 'Install ccache to run this integration test'
with tempfile.TemporaryDirectory(prefix='glob2-ccache-') as directory:
    work = Path(directory)
    child_env = dict(os.environ, CCACHE='1', CCACHE_DIR=str(work / 'cache'),
                     CCACHE_COMPILERCHECK='content')
    # Isolate cache configuration from the developer's user configuration.
    config = work / 'ccache.conf'
    config.write_text('sloppiness =\n')
    child_env['CCACHE_CONFIGPATH'] = str(config)
    (work / 'SConstruct').write_text(
        'import os, sys\n'
        f'sys.path.insert(0, {str(root / "scons")!r})\n'
        'import ccache\n'
        'env = Environment(ENV=dict(os.environ))\n'
        'ccache.enable(env)\n'
        'env.Program("probe", "probe.cpp")\n')
    (work / 'probe.cpp').write_text('#include "value.h"\nint main() { return VALUE; }\n')
    header = work / 'value.h'
    header.write_text('#define VALUE 7\n')

    def build(expected):
        result = subprocess.run(['scons', '-Q'], cwd=work, env=child_env,
                                text=True, capture_output=True, check=True)
        lines = result.stdout.splitlines()
        compile_lines = [line for line in lines if ' -c ' in line]
        link_lines = [line for line in lines if ' -o probe ' in line]
        assert compile_lines and all('ccache' in line for line in compile_lines), lines
        assert link_lines and all('ccache' not in line for line in link_lines), lines
        assert subprocess.run([str(work / 'probe')]).returncode == expected
        stats = subprocess.check_output([ccache, '--print-stats'], env=child_env, text=True)
        return {key: int(value) for key, value in (line.split() for line in stats.splitlines())}

    cold = build(7)
    (work / 'probe.o').unlink()
    (work / 'probe').unlink()
    warm = build(7)
    hits = lambda stats: stats.get('direct_cache_hit', 0) + stats.get('preprocessed_cache_hit', 0)
    assert hits(warm) > hits(cold), (cold, warm)
    header.write_text('#define VALUE 9\n')
    edited = build(9)
    assert edited['cache_miss'] > warm['cache_miss'], (warm, edited)
print('ccache: cold/warm builds, header invalidation, unwrapped linking and wrapper guards PASS')
