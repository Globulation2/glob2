"""Checksum-pinned Ogg Opus decoder for serial and threaded WebAssembly."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import tempfile
import subprocess
import tarfile

from sdl3_dependencies import download

LOCK = Path(__file__).with_name('opus-versions.json')
LIBRARIES = ('opusfile', 'opus', 'ogg')


def build(prefix, work, emscripten, environment=None, threaded=False, jobs=2):
    prefix, work, emscripten = (Path(p).resolve() for p in (prefix, work, emscripten))
    env = dict(environment if environment is not None else os.environ)
    versions = json.loads(LOCK.read_text())
    cc = emscripten / 'emcc'
    flags = ['-O2', '-fwasm-exceptions'] + (['-pthread'] if threaded else [])
    identity = dict(versions=versions, threaded=threaded, flags=flags,
                    compiler=subprocess.check_output([str(cc), '--version'], env=env, text=True),
                    sdk=str(emscripten), recipe=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    manifest = prefix / 'opus-manifest.json'
    archives = [prefix / 'lib' / ('lib' + name + '.a') for name in LIBRARIES]
    if manifest.exists():
        installed = json.loads(manifest.read_text())
        if installed.get('identity') == identity and all(p.is_file() and
                hashlib.sha256(p.read_bytes()).hexdigest() == installed.get('archives', {}).get(p.name)
                for p in archives):
            return prefix
    download(work, versions)
    prefix.mkdir(parents=True, exist_ok=True)
    env.update(CFLAGS=' '.join(flags), LDFLAGS=' '.join(flags),
               PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'),
               PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'))
    env.pop('CONFIG_SITE', None)
    # Autoconf expands $CC/$AR as shell words. macOS's managed SDK lives in
    # Library/Application Support; private wrappers preserve those paths.
    with tempfile.TemporaryDirectory(prefix='glob2-opus-tools-', dir='/tmp' if os.name != 'nt' else None) as commands:
        for variable, command in {'CC': cc, 'AR': emscripten / 'emar',
                                  'RANLIB': emscripten / 'emranlib',
                                  'NM': emscripten.parent / 'bin/llvm-nm'}.items():
            wrapper = Path(commands) / variable.lower()
            wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([str(command)]) + ' "$@"\n')
            wrapper.chmod(0o700)
            env[variable] = str(wrapper)
        for name, spec in versions.items():
            source = work / spec['directory']
            if not source.exists():
                with tarfile.open(work / spec['archive']) as package:
                    package.extractall(work, filter='data')
            if name == 'opusfile':
                # opusfile 0.12 predates wasm32. Use the GNU target recognition
                # scripts from the checksum-pinned Ogg release, not the host system.
                for script in ('config.sub', 'config.guess'):
                    shutil.copyfile(work / versions['ogg']['directory'] / script, source / script)
            output = work / (name + '-build')
            # Invalidate configure caches and objects when flags/toolchain change.
            if output.exists(): shutil.rmtree(output)
            output.mkdir()
            options = ['--host=wasm32-unknown-emscripten', '--prefix=' + str(prefix),
                       '--libdir=' + str(prefix / 'lib'), '--disable-shared', '--enable-static']
            if name == 'opus': options += ['--disable-extra-programs', '--disable-doc', '--disable-intrinsics']
            if name == 'opusfile': options += ['--disable-http', '--disable-examples', '--disable-doc']
            subprocess.run(['bash', str(source / 'configure'), *options], cwd=output, env=env, check=True)
            subprocess.run(['make', '-j' + str(jobs)], cwd=output, env=env, check=True)
            subprocess.run(['make', 'install'], cwd=output, env=env, check=True)
            notice = prefix / 'share/licenses/opus' / (name + '-COPYING')
            notice.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / 'COPYING', notice)
    manifest.write_text(json.dumps(dict(identity=identity, archives={
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in archives}), indent=2) + '\n')
    return prefix
