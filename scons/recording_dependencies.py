"""Pinned, minimal recording libraries; never discover host FFmpeg libraries."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile
import tempfile

from sdl3_dependencies import download

LOCK = Path(__file__).with_name('recording-versions.json')
LIBRARIES = ('avformat', 'avcodec', 'swscale', 'swresample', 'avutil', 'x264')


def build(prefix, work, *, cc='cc', cxx='c++', ar='ar', ranlib='ranlib',
          target=None, arch=None, cflags=(), ldflags=(), environment=None, jobs=2, sdk_identity=None):
    prefix, work = Path(prefix).resolve(), Path(work).resolve()
    env = dict(environment if environment is not None else os.environ)
    versions = json.loads(LOCK.read_text())
    target = target or {'Darwin': 'darwin', 'Windows': 'mingw32'}.get(platform.system(), 'linux')
    arch = arch or platform.machine()
    arch = {'AMD64':'x86_64','ARM64':'aarch64'}.get(arch,arch)
    if sdk_identity is None and target == 'darwin':
        sdk_identity = {key: subprocess.check_output(['xcrun', '--show-sdk-'+key],env=env,text=True).strip() for key in ('path','version')}
    cflags, ldflags = list(cflags), list(ldflags)
    vaapi = None
    if target == 'linux':
        probe_env = dict(env)
        probe_env.pop('PKG_CONFIG_LIBDIR', None)
        try:
            probe = subprocess.run(['pkg-config', '--modversion', 'libva', 'libva-drm'],
                                   env=probe_env, capture_output=True, text=True)
            if probe.returncode == 0: vaapi = probe.stdout.strip()
        except FileNotFoundError:
            pass
    compiler = subprocess.check_output(shlex.split(str(cc)) + ['--version'], env=env, text=True)
    identity = dict(configuration=1, versions=versions, target=target, arch=arch,
                    compiler=compiler, cc=str(cc), ar=str(ar), flags=[cflags, ldflags],
                    vaapi_api=vaapi, sdk=sdk_identity, environment={name:env.get(name,'') for name in ('CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','MACOSX_DEPLOYMENT_TARGET','DEVELOPER_DIR')}, recipe=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    manifest = prefix / 'recording-manifest.json'
    archives = [prefix / 'lib' / ('lib' + name + '.a') for name in LIBRARIES]
    if manifest.exists():
        installed = json.loads(manifest.read_text())
        if installed.get('identity') == identity and all(p.is_file() and
                hashlib.sha256(p.read_bytes()).hexdigest() == installed.get('archives', {}).get(p.name)
                for p in archives):
            return prefix
    work.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    vendored = Path(env.get('GLOB2_RECORDING_ARCHIVES', str(LOCK.parent.parent / 'third_party/recording-sources')))
    for spec in versions.values():
        if not (work / spec['archive']).exists() and (vendored / spec['archive']).exists():
            shutil.copy2(vendored / spec['archive'], work / spec['archive'])
    download(work, versions)
    sources = {}
    for name, spec in versions.items():
        source = work / spec['directory']
        if not source.exists():
            with tarfile.open(work / spec['archive']) as package:
                package.extractall(work, filter='data')
        sources[name] = source
    # x264 expands $CC without eval, so a quoted SDK path is still split. Use
    # private executable wrappers with space-free names for both upstream builds.
    commands = tempfile.TemporaryDirectory(prefix='glob2-recording-tools-',dir='/tmp' if platform.system()!='Windows' else None)
    def wrapper(name, command):
        path = Path(commands.name) / name
        path.write_bytes(('#!/bin/sh\nexec ' + shlex.join(shlex.split(str(command))) + ' "$@"\n').encode())
        path.chmod(0o700)
        return path.as_posix()
    original_ar = ar
    cc, cxx, ar, ranlib = (wrapper(name, command) for name, command in
                           [('cc',cc),('cxx',cxx),('ar',ar),('ranlib',ranlib)])
    env.update(CC=cc, CXX=cxx, AR=ar, RANLIB=ranlib)
    env['PKG_CONFIG_PATH'] = (prefix / 'lib/pkgconfig').as_posix()
    if target != 'linux': env['PKG_CONFIG_LIBDIR'] = env['PKG_CONFIG_PATH']
    else: env.pop('PKG_CONFIG_LIBDIR', None)
    extra_c = shlex.join(['-O3', '-fPIC'] + cflags)
    extra_ld = shlex.join(ldflags)
    x264 = ['sh', (sources['x264'] / 'configure').as_posix(), '--prefix=' + prefix.as_posix(),
            '--enable-static', '--enable-pic', '--disable-cli', '--disable-opencl',
            '--disable-avs', '--disable-lavf', '--disable-swscale', '--disable-ffms',
            '--bit-depth=8', '--chroma-format=420', '--extra-cflags=' + extra_c,
            '--extra-ldflags=' + extra_ld]
    if arch in ('arm64','arm64-v8a','armeabi-v7a','aarch64'):
        x264 += ['--extra-asflags=' + shlex.join(cflags)]
    encoders = ['libx264', 'aac']
    hardware = []
    if target == 'wasm':
        x264 += ['--host=wasm32-unknown-linux', '--disable-asm', '--disable-thread']
    elif target == 'android':
        x264 += ['--host=' + {'arm64-v8a': 'aarch64-linux-android', 'armeabi-v7a': 'arm-linux-androideabi', 'x86_64': 'x86_64-linux-android'}[arch]]
        encoders += ['h264_mediacodec']; hardware = ['--enable-mediacodec','--enable-jni']
    elif target in ('darwin', 'ios'):
        if target == 'ios': x264 += ['--host=aarch64-apple-darwin']
        encoders += ['h264_videotoolbox']; hardware = ['--enable-videotoolbox']
    elif target == 'mingw32':
        x264 += ['--host=x86_64-w64-mingw32']
        encoders += ['h264_mf', 'h264_nvenc']; hardware = ['--enable-mediafoundation','--enable-ffnvcodec','--enable-nvenc']
    elif target == 'linux':
        # VAAPI is linked only to its runtime API, never to codec implementations.
        encoders += ['h264_nvenc']; hardware = ['--enable-ffnvcodec','--enable-nvenc']
        if vaapi: encoders += ['h264_vaapi']; hardware += ['--enable-vaapi']
    if target in ('linux', 'mingw32'):
        subprocess.run(['make', 'install', 'PREFIX=' + prefix.as_posix()], cwd=sources['nv-codec-headers'], env=env, check=True)
    ffmpeg = ['sh', (sources['ffmpeg'] / 'configure').as_posix(), '--prefix=' + prefix.as_posix(),
              '--disable-autodetect', '--disable-everything', '--disable-programs',
              '--disable-doc', '--disable-debug', '--disable-network', '--disable-avdevice',
              '--disable-avfilter', '--enable-static', '--disable-shared', '--enable-pic', '--enable-gpl',
              '--enable-libx264', '--enable-encoder=' + ','.join(encoders),
              '--enable-muxer=mp4', '--enable-demuxer=mov', '--enable-swscale',
              '--enable-swresample', '--cc=' + str(cc), '--cxx=' + str(cxx),
              '--ar=' + str(ar), '--ranlib=' + str(ranlib),
              '--extra-cflags=' + shlex.join(['-O3', '-fPIC', '-I' + (prefix / 'include').as_posix()] + cflags),
              '--extra-ldflags=' + shlex.join(['-L' + (prefix / 'lib').as_posix()] + ldflags)] + hardware
    if target in ('wasm', 'android', 'ios', 'mingw32'):
        mapped = {'arm64-v8a': 'aarch64', 'armeabi-v7a': 'arm', 'arm64': 'aarch64'}.get(arch, arch)
        ffmpeg += ['--enable-cross-compile', '--target-os=' + {'wasm': 'none', 'ios': 'darwin'}.get(target,target),
                   '--arch=' + ('wasm32' if target == 'wasm' else mapped)]
    if target == 'wasm':
        # A dedicated worker needs no pthreads; preserve wasm32 SIMD detection.
        ffmpeg += ['--disable-pthreads', '--disable-w32threads', '--disable-os2threads',
                   '--nm=' + wrapper('nm', shlex.quote(str(Path(shlex.split(str(original_ar))[0]).parent.parent / 'bin/llvm-nm')))]
    else:
        ffmpeg += ['--enable-pthreads' if target != 'mingw32' else '--enable-w32threads']
    for name, command in [('x264', x264), ('ffmpeg', ffmpeg)]:
        directory = work / (name + '-build')
        if directory.exists(): shutil.rmtree(directory)
        directory.mkdir()
        with (directory / 'build.log').open('w') as log:
            for index,step in enumerate((command, ['make', '-j' + str(jobs)], ['make', 'install'])):
                result = subprocess.run(step, cwd=directory, env=env, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError('Recording dependency failed: ' + name + '; see ' + str(directory / 'build.log'))
                if index == 0 and name == 'x264' and target == 'wasm':
                    # Upstream disables C vectorization when native assembler is
                    # present. wasm32 has no x264 assembler; retain LLVM SIMD.
                    configuration=directory/'config.mak'
                    with configuration.open('a') as flags:
                        flags.write('\nCFLAGS += -fvectorize -fslp-vectorize\n')
    notices = prefix / 'share/licenses/recording'
    notices.mkdir(parents=True, exist_ok=True)
    licenses=[('ffmpeg',p.name) for p in sources['ffmpeg'].glob('COPYING.*')] + [('x264','COPYING')]
    for name, filename in licenses:
        shutil.copy2(sources[name] / filename, notices / (name + '-' + filename))
    shutil.copy2(sources['nv-codec-headers']/'include/ffnvcodec/nvEncodeAPI.h',notices/'nvEncodeAPI.h')
    (notices/'configuration.json').write_text(json.dumps(dict(target=target,arch=arch,encoders=encoders,hardware=hardware,ffmpeg=[flag for flag in ffmpeg if flag.startswith(('--disable-','--enable-','--arch=','--target-os='))],x264=[flag for flag in x264 if flag.startswith(('--disable-','--enable-','--bit-depth=','--chroma-format='))],wasm_vectorization=target=='wasm',compiler=compiler.splitlines()[0],flags=[cflags,ldflags]),indent=2)+'\n')
    (notices / 'sources.json').write_text(json.dumps(versions, indent=2) + '\n')
    manifest.write_text(json.dumps(dict(identity=identity, archives={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in archives}), indent=2) + '\n')
    commands.cleanup()
    return prefix


def attach(env, prefix, target):
    prefix = Path(prefix)
    env.Prepend(CPPPATH=[str(prefix / 'include')])
    env.Append(LIBS=[env.File(str(prefix / 'lib' / ('lib' + name + '.a'))) for name in LIBRARIES])
    if target in ('darwin', 'ios'):
        env.Append(FRAMEWORKS=['VideoToolbox', 'CoreMedia', 'CoreVideo', 'Foundation', 'AudioToolbox'])
    elif target == 'android':
        env.Append(LIBS=['mediandk'])
        # Static FFmpeg assembly references private tables directly. Bind only
        # codec archive symbols locally inside the JNI shared library; SDL's
        # Java entry points must remain exported.
        env.Append(LINKFLAGS=['-Wl,--exclude-libs=' + ':'.join('lib'+name+'.a' for name in LIBRARIES)])
    elif target == 'mingw32': env.Append(LIBS=['mfplat', 'mfuuid', 'ole32', 'strmiids', 'bcrypt'])
    elif target == 'linux':
        manifest = prefix / 'recording-manifest.json'
        if manifest.exists() and json.loads(manifest.read_text())['identity'].get('vaapi_api'):
            env.Append(LIBS=['va', 'va-drm'])
        env.Append(LIBS=['dl', 'm'])


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    build(args.prefix,args.work,cc=args.cc,cxx=args.cxx,jobs=args.jobs)
