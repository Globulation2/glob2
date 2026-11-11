#!/usr/bin/env python3
"""Command-line entry points through which the CMake build calls
the same generators SCons uses, so both build systems write identical files.

Every subcommand writes only when the content changed, keeping timestamps (and
therefore Ninja and compiler-cache state) stable. Nothing here imports SCons.
"""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

from build_layout import PACKAGE_VERSION, build_identity, default_directory, write_if_changed  # noqa: E402


def _touch(stamp):
    if stamp:
        Path(stamp).parent.mkdir(parents=True, exist_ok=True)
        Path(stamp).touch()


def build_arguments(args):
    """The SCons-style key=value build arguments CMake was configured with."""
    arguments = {}
    for word in getattr(args, 'arg', None) or []:
        key, separator, value = word.partition('=')
        if not separator or not key:
            raise SystemExit('build arguments are key=value words, got ' + repr(word))
        arguments[key] = value
    return arguments


def build_config(args):
    import build_config
    identity = build_identity(build_arguments(args))
    config = build_config.ConfigurationText()
    build_config.add_package(config, datadir=args.datadir, version=PACKAGE_VERSION, font=args.font,
                             darwin=False, windows=False, china=bool(identity.get('china')), identity=identity)
    build_config.add_native_wss(config)
    if args.opengl:
        build_config.add_opengl(config)
    if args.fribidi:
        build_config.add_fribidi(config)
    write_if_changed(Path(args.builddir) / 'include/glob2/BuildConfig.h', config.text())


def compile_profile(args):
    """glob2/CompileProfile.h, as SConstruct writes it for every native build."""
    from sources import profile_header
    write_if_changed(Path(args.builddir) / 'include/glob2/CompileProfile.h', profile_header(args.profile))


def build_paths(args):
    import checkout_paths
    checkout_paths.write(args.builddir, PACKAGE_SOURCE_DIR=str(ROOT),
                         PACKAGE_RUNTIME_ASSET_DIR=args.runtime_assets,
                         GLOB2_RENDER_SHADER_DIR=str((Path(args.builddir) / 'shaders').resolve()))


def build_stamp(args):
    import checkout_paths
    checkout_paths.write_stamp(args.builddir, ROOT)


def skin_materials(args):
    import skin_materials
    skin_materials.generate(ROOT, args.builddir)
    _touch(args.stamp)


def shaders(args):
    import renderer_shaders
    renderer_shaders.verify_compute(ROOT)
    renderer_shaders.prepare(ROOT, Path(args.builddir))
    _touch(args.stamp)


def shader_inputs(args):
    import renderer_shaders
    print(';'.join(path.as_posix() for path in renderer_shaders.inputs(ROOT)))


def hive_api(args):
    import hive_api
    source = ROOT / 'services/online/packages/hive/src/commander-api.txt'
    write_if_changed(Path(args.builddir) / 'include/hive/PlayerAPIContract.h',
                     hive_api.render(source.read_text(encoding='utf-8')))
    _touch(args.stamp)


def skin_render(args):
    import skin_render
    write_if_changed(Path(args.builddir) / 'src/SkinRenderRecipe.h',
                     skin_render.content(ROOT, skin_render.inputs(ROOT)))
    _touch(args.stamp)


def skin_render_inputs(args):
    import skin_render
    print(';'.join(path.as_posix() for path in skin_render.inputs(ROOT)))


def official_instance(args):
    import official_instance
    origin = official_instance.origin()
    print(json.dumps({'origin': origin, 'former': ' '.join(official_instance.former_origins(origin))}))


def recording(args):
    """The h264 recording archives and system libraries recording_dependencies.attach links."""
    import recording_dependencies
    prefix = Path(args.prefix).resolve()
    manifest = prefix / 'recording-manifest.json'
    identity = json.loads(manifest.read_text())['identity'] if manifest.exists() else {}
    if identity.get('video_codec', 'h264') != 'h264':
        raise SystemExit('Recording prefix codec does not match the h264 CMake build')
    archives = [(prefix / 'lib' / ('lib' + name + '.a')).as_posix()
                for name in recording_dependencies.codec_libraries('h264')]
    system = (['va', 'va-drm'] if identity.get('vaapi_api') else []) + ['dl', 'm']
    print(';'.join(archives + system))


def recording_prefix(args):
    """Provision the pinned h264 recording libraries as SConstruct does and print their prefix.

    GLOB2_RECORDING_PREFIX, or a prefix with recording/ beside GLOB2_SDL3_PREFIX,
    is used as given; otherwise the shared dependency store builds or reuses one
    keyed by the same recipe and compilers SCons uses."""
    from recording_dependencies import build as build_recording
    from shared_dependencies import ensure
    sdl = os.environ.get('GLOB2_SDL3_PREFIX')
    shared = Path(sdl) / 'recording' if sdl else None
    has_shared = bool(shared and (shared / 'recording-manifest.json').exists())
    explicit = os.environ.get('GLOB2_RECORDING_PREFIX')
    builddir = Path(args.builddir).resolve()
    prefix = Path(explicit) if explicit else shared if has_shared else builddir / 'recording/prefix'
    # CMake reads the prefix from stdout: send the builders' output (and their
    # child processes') to stderr meanwhile.
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        prefix = ensure(build_recording, prefix.resolve(), builddir / 'recording/sources',
                        explicit=bool(explicit or has_shared), execute=True,
                        cc=args.cc, cxx=args.cxx, ar=args.ar, ranlib=args.ranlib, target='linux',
                        video_codec='h264', opus_configuration=None, environment=dict(os.environ), jobs=args.jobs)
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    print(Path(prefix).resolve().as_posix())


def lean_image_prefix(args):
    """The private PNG/JPEG/WebP SDL_image a lean_images=1 release links, built on
    first use as SConstruct does; also records it in image-runtime.json."""
    from native_image_dependency import ensure
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        prefix = ensure(ROOT, cc=args.cc, cxx=args.cxx, jobs=args.jobs, environment=dict(os.environ))
        sys.stdout.flush()
    finally:
        os.dup2(saved, 1)
        os.close(saved)
    write_if_changed(Path(args.builddir) / 'image-runtime.json', json.dumps({'prefix': str(prefix)}) + '\n')
    print(Path(prefix).resolve().as_posix())


# content/data files installed beside the game, as `scons install` installed them.
DESKTOP_FILES = (('content/data/glob2.desktop', 'applications/org.globulation2.Globulation2.desktop'),
                 ('content/data/org.globulation2.Globulation2.metainfo.xml',
                  'metainfo/org.globulation2.Globulation2.metainfo.xml'),
                 ('content/data/completions/glob2.bash', 'bash-completion/completions/glob2'),
                 ('content/data/completions/_glob2', 'zsh/site-functions/_glob2'),
                 ('content/data/completions/glob2.fish', 'fish/vendor_completions.d/glob2.fish'),
                 ('platforms/linux/glob2.6', 'man/man6/glob2.6'),
                 *(('content/data/icons/glob2-icon-%dx%d.png' % (size, size),
                    'icons/hicolor/%dx%d/apps/glob2.png' % (size, size)) for size in (16, 24, 32, 48, 64, 128, 256)))


def _copy(source, destination):
    """Copy a file, a symbolic link as a link, as SCons Install does."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink() or destination.exists():
        destination.unlink()
    if source.is_symlink():
        destination.symlink_to(os.readlink(source))
    else:
        import shutil
        shutil.copy2(source, destination)


def install(args):
    """Everything `scons install` installs beside the glob2 executable, under $DESTDIR.

    The verified runtime-asset tree merges into <installdir>/glob2
    (runtime_assets.install_export); then the shaders, desktop integration,
    completions and manual; the SDL3 runtime and its notices when the build
    used GLOB2_SDL3_PREFIX; the recording notices; and a lean_images build's
    private SDL_image with its soname links and notice."""
    import runtime_assets
    destdir = os.environ.get('DESTDIR', '')
    def staged(path):
        return Path(destdir + str(path)) if destdir else Path(path)
    builddir = Path(args.builddir).resolve()
    installdir, bindir = Path(args.installdir), Path(args.bindir)
    runtime_assets.install_export(builddir / 'runtime-assets', staged(installdir / 'glob2'))
    shaders = builddir / 'shaders'
    for path in sorted(shaders.rglob('*')):
        relative = path.relative_to(shaders)
        if (path.is_file() and not path.name.startswith('.') and 'native-sources' not in relative.parts
                and path.suffix not in ('.h', '.air')):
            _copy(path, staged(installdir / 'glob2/shaders' / relative))
    for source, destination in DESKTOP_FILES:
        _copy(ROOT / source, staged(installdir / destination))
    libraries = staged(bindir.parent / 'lib/glob2')
    if args.sdl_prefix:
        sdl = Path(args.sdl_prefix).resolve()
        for path in sorted((sdl / 'lib').glob('libSDL3*.so*')):
            if not (args.lean_image_prefix and path.name.startswith('libSDL3_image')):
                _copy(path, libraries / path.name)
        for notice in sorted((sdl / 'share/licenses').glob('SDL3*/LICENSE.txt')):
            _copy(notice, staged(installdir / 'glob2/licenses' / notice.parent.name / notice.name))
    if args.recording_prefix:
        for notice in sorted((Path(args.recording_prefix) / 'share/licenses/recording').glob('*')):
            _copy(notice, staged(installdir / 'glob2/licenses/recording' / notice.name))
    if args.lean_image_prefix:
        # install_image_runtime.install_runtime: one real library, soname links to it.
        prefix = Path(args.lean_image_prefix)
        sources = sorted((prefix / 'lib').glob('libSDL3_image*.so*'))
        real = {path.resolve() for path in sources}
        if len(real) != 1:
            raise SystemExit('Lean image prefix must contain exactly one SDL_image runtime')
        canonical = next(iter(real))
        _copy(canonical, libraries / canonical.name)
        for path in sources:
            if path.name != canonical.name:
                link = libraries / path.name
                if link.is_symlink() or link.exists():
                    link.unlink()
                link.symlink_to(canonical.name)
        notice = prefix / 'share/licenses/SDL3_image/LICENSE.txt'
        if not notice.is_file():
            raise SystemExit('Lean image prefix is missing its license notice')
        _copy(notice, staged(installdir / 'doc/glob2/SDL3_image-LICENSE.txt'))


def ccache_directory(args):
    """The managed compiler cache SCons uses, configured the same way."""
    if os.environ.get('CCACHE_DIR'):
        import dev_store
        print(dev_store.adopt(os.environ['CCACHE_DIR']))
        return
    import ccache
    import dev_store
    managed = dev_store.cache(ROOT, 'ccache', lease=False)
    ccache.configure_managed(managed)
    print(managed)


def configure(args):
    """Resolve the build arguments as SCons does, write identity.json and print the identity.

    test/run_tests.py, tools/perf_ab.py and tools/dev_baseline.py read identity.json;
    the generator key keeps SCons from adopting the directory
    (build_layout.prepare_directory refuses a mismatch). CMake reads the printed
    JSON: the resolved identity and its default build directory."""
    arguments = build_arguments(args)
    try:
        resolved = build_identity(arguments)
    except ValueError as error:
        raise SystemExit('invalid build arguments: ' + str(error))
    if resolved['target'] != 'native' or resolved['toolchain'] != 'linux':
        raise SystemExit('the CMake build supports native Linux builds; got ' + json.dumps(resolved))
    content = dict(resolved, generator='cmake')
    write_if_changed(Path(args.builddir) / 'identity.json', json.dumps(content, sort_keys=True, indent=2) + '\n')
    print(json.dumps({'identity': resolved, 'directory': (ROOT / default_directory(resolved)).as_posix(),
                      'arguments': arguments}, sort_keys=True))


def test_provenance(args):
    """<binary>.provenance.json, through the code test/SConscript's sidecar runs."""
    import test_provenance
    configuration = json.loads(Path(args.configuration).read_text())
    test_provenance.write_sidecar(ROOT, args.binary, args.binary + '.provenance.json', configuration)


def provenance_configuration(args):
    """The compiler and flags a CMake provenance sidecar records, in test_provenance's shape."""
    configuration = dict(buildSystem='cmake', compiler=args.compiler, flags={
        'CCFLAGS': args.ccflags, 'CXXFLAGS': ' '.join(args.cxxflags.split()),
        'CPPDEFINES': ' '.join(args.define), 'LINKFLAGS': ' '.join(args.linkflag)})
    write_if_changed(args.output, json.dumps(configuration, sort_keys=True, indent=2) + '\n')


def _depfile_path(path):
    return str(path).replace(' ', '\\ ').replace('#', '\\#').replace('$', '$$')


def runtime_assets(args):
    """Export the runtime assets as SCons' dev tier does, then write a Ninja depfile.

    The export (runtime_assets.export_tree) clones a finished tree from the
    development store when one with the same content key exists. The depfile
    lists every input and the directories holding them, so Ninja re-runs the
    export when an input changes or a directory gains or loses a file."""
    import runtime_assets
    builddir = Path(args.builddir).resolve()
    platform = runtime_assets.host_platform()
    inputs = runtime_assets.export_inputs(ROOT, platform)
    # As prepare_assets: the release flag and the asset options pick the profile;
    # the CMake build always has h264 recording, so video packing is available.
    arguments = build_arguments(args)
    release = build_identity(arguments)['mode'] == 'release'
    lossy = runtime_assets.optimized_install_enabled(release, arguments.get('optimized_assets', 'auto'))
    packing = runtime_assets.sprite_packing_profile(arguments.get('sprite_packing', 'auto'), release, True)
    runtime_assets.export_tree(ROOT, builddir / 'runtime-assets', platform, inputs, lossy, packing,
                               packing == 'video', args.jobs)
    files = [Path(path).resolve() for path in inputs] + [ROOT / path for path in runtime_assets.EXPORT_TOOLS]
    directories = sorted({path.parent for path in files})
    names = [_depfile_path(path) for path in [*files, *directories]]
    write_if_changed(args.depfile, _depfile_path(builddir / 'runtime-assets.json') + ': '
                     + ' \\\n  '.join(names) + '\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)

    def command(name, function, builddir=True, stamp=False):
        sub = commands.add_parser(name)
        sub.set_defaults(function=function)
        if builddir:
            sub.add_argument('--builddir', required=True)
        if stamp:
            sub.add_argument('--stamp')
        return sub

    sub = command('build-config', build_config)
    sub.add_argument('--datadir', default='/usr/local/share')
    sub.add_argument('--font', default='sans.ttf')
    sub.add_argument('--opengl', action='store_true')
    sub.add_argument('--fribidi', action='store_true')
    sub.add_argument('--arg', action='append', default=[])
    sub = command('build-paths', build_paths)
    sub.add_argument('--runtime-assets', required=True)
    sub = command('compile-profile', compile_profile)
    sub.add_argument('--profile', required=True)
    command('build-stamp', build_stamp)
    command('skin-materials', skin_materials, stamp=True)
    command('shaders', shaders, stamp=True)
    command('shader-inputs', shader_inputs, builddir=False)
    command('hive-api', hive_api, stamp=True)
    command('skin-render', skin_render, stamp=True)
    command('skin-render-inputs', skin_render_inputs, builddir=False)
    command('official-instance', official_instance, builddir=False)
    sub = command('recording', recording, builddir=False)
    sub.add_argument('--prefix', required=True)
    sub = command('recording-prefix', recording_prefix)
    # SCons' compiler names (its CC/CXX defaults), so both share one store entry.
    sub.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    sub.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    sub.add_argument('--ar', default='ar')
    sub.add_argument('--ranlib', default='ranlib')
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('lean-image-prefix', lean_image_prefix)
    sub.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    sub.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    sub.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    sub = command('install', install)
    sub.add_argument('--installdir', required=True)
    sub.add_argument('--bindir', required=True)
    sub.add_argument('--sdl-prefix', default='')
    sub.add_argument('--recording-prefix', default='')
    sub.add_argument('--lean-image-prefix', default='')
    command('ccache-directory', ccache_directory, builddir=False)
    sub = command('configure', configure)
    sub.add_argument('--arg', action='append', default=[])
    sub = command('test-provenance', test_provenance, builddir=False)
    sub.add_argument('--binary', required=True)
    sub.add_argument('--configuration', required=True)
    sub = command('provenance-configuration', provenance_configuration, builddir=False)
    sub.add_argument('--output', required=True)
    sub.add_argument('--compiler', required=True)
    sub.add_argument('--ccflags', default='')
    sub.add_argument('--cxxflags', default='')
    sub.add_argument('--define', action='append', default=[])
    sub.add_argument('--linkflag', action='append', default=[])
    sub = command('runtime-assets', runtime_assets)
    sub.add_argument('--depfile', required=True)
    sub.add_argument('--arg', action='append', default=[])
    sub.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args(argv)
    args.function(args)


if __name__ == '__main__':
    main()
