#!/usr/bin/env python3
"""Real GCC/Clang builds for PCH/unity, invalidation, caching and object reuse."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='glob2-dev-compile-') as temporary:
    root = Path(temporary)
    (root / 'scons').mkdir()
    (root / 'scons/unity-sources.json').write_text(json.dumps(['code/a.cpp', 'code/b.cpp']))
    (root / 'tools/javascript').mkdir(parents=True)
    shutil.copy2(ROOT / 'tools/javascript/check-math-symbols.py', root / 'tools/javascript/check-math-symbols.py')
    (root / 'code').mkdir()
    (root / 'code/value.h').write_text('#pragma once\n#define VALUE 7\n')
    (root / 'code/a.cpp').write_text('#include "value.h"\n#include <vector>\nint a() { return std::vector<int>{VALUE}[0]; }\n')
    (root / 'code/b.cpp').write_text('#include <string>\nint b() { return std::string("x").size() - 1; }\n')
    (root / 'code/main.cpp').write_text('int a(); int b(); int main() { return a()+b(); }\n')
    (root / 'SConstruct').write_text(f'''import os, sys
sys.path.insert(0, {str(ROOT / 'scons')!r})
from dev_compile import objects, unique
from dev_build import configure
import ccache
identity = {{'target':'native','toolchain':'linux', 'dev_fast':True, 'pch':ARGUMENTS.get('pch')=='1', 'unity':ARGUMENTS.get('unity')=='1'}}
build = 'build-' + ARGUMENTS.get('pch','0') + ARGUMENTS.get('unity','0')
env = Environment(ENV=dict(os.environ), CXX=ARGUMENTS.get('CXX','g++'))
env['BUILDDIR'] = build
env.Append(CXXFLAGS=['-std=gnu++20'])
env.Tool('compilation_db')
configure(env, identity)
ccache.enable(env)
files = ['code/a.cpp','code/b.cpp','code/main.cpp']
mapping = objects(env, files, lambda name: build+'/'+name+'.o')
from javascript import numeric_guard
numeric_guard(env, unique(mapping.values()))
env.Default(env.Program(build+'/probe', unique(mapping.values())))
env.Default(env.CompilationDatabase(build+'/compile_commands.json'))
''')
    environment = dict(os.environ, CCACHE='1', CCACHE_DIR=str(root / 'cache'), CCACHE_COMPILERCHECK='content', GLOB2_BUILD_TIMING_LOG=str(root / 'actions.jsonl'))
    for compiler in filter(None, [shutil.which('g++'), os.environ.get('GLOB2_TEST_CXX') or shutil.which('clang++')]):
        for pch, unity in [('0','0'), ('1','0'), ('0','1'), ('1','1')]:
            arguments = ['scons', '-Q', '-j2', 'CXX='+compiler, 'pch='+pch, 'unity='+unity]
            first = subprocess.run(arguments, cwd=root, env=environment, capture_output=True, text=True)
            assert first.returncode == 0, first.stdout + first.stderr
            events = [json.loads(line) for line in (root / 'actions.jsonl').read_text().splitlines()]
            assert all(event['phase'] == 'compile' and event['end'] >= event['start'] for event in events)
            program = root / ('build-'+pch+unity+'/probe' + ('.exe' if os.name == 'nt' else ''))
            assert subprocess.run([program]).returncode == 7
            second = subprocess.run(arguments, cwd=root, env=environment, capture_output=True, text=True, check=True)
            assert ' -c ' not in second.stdout and ' -o ' not in second.stdout, second.stdout
            (root / 'code/value.h').write_text('#pragma once\n#define VALUE 9\n')
            edited = subprocess.run(arguments, cwd=root, env=environment, capture_output=True, text=True, check=True)
            assert subprocess.run([program]).returncode == 9
            (root / 'code/value.h').write_text('#pragma once\n#define VALUE 7\n')
            database = json.loads((program.parent / 'compile_commands.json').read_text())
            if unity == '1':
                assert any('code/a.cpp' in entry['file'] for entry in database)
                assert any('code/b.cpp' in entry['file'] for entry in database)
            print(compiler, 'pch='+pch, 'unity='+unity, 'PASS')
print('development compilation: configurations, header invalidation and no-change builds PASS')
