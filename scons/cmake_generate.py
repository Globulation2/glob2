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


def build_config(args):
    import build_config
    config = build_config.ConfigurationText()
    build_config.add_package(config, datadir=args.datadir, version=PACKAGE_VERSION, font=args.font,
                             darwin=False, windows=False, china=False, identity=build_identity({}))
    build_config.add_native_wss(config)
    if args.opengl:
        build_config.add_opengl(config)
    if args.fribidi:
        build_config.add_fribidi(config)
    write_if_changed(Path(args.builddir) / 'include/glob2/BuildConfig.h', config.text())


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


def scons_dev_directory(args):
    """The SCons fast-development build directory (tools/dev_build.py defaults)."""
    print((ROOT / default_directory(build_identity(DEV_IDENTITY))).as_posix())


DEV_IDENTITY = {'dev_fast': 1, 'linker': 'auto'}


def identity(args):
    """identity.json as SCons writes it for the dev tier, marked as a CMake build.

    test/run_tests.py reads the mode from it; the extra key keeps SCons from
    adopting the directory (build_layout.prepare_directory refuses a mismatch)."""
    content = dict(build_identity(DEV_IDENTITY), generator='cmake')
    write_if_changed(Path(args.builddir) / 'identity.json', json.dumps(content, sort_keys=True, indent=2) + '\n')


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
    # SCons' dev tier: release=0, optimized_assets=auto, sprite_packing=auto, h264 recording.
    lossy = runtime_assets.optimized_install_enabled(False, 'auto')
    packing = runtime_assets.sprite_packing_profile('auto', False, True)
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
    sub = command('build-paths', build_paths)
    sub.add_argument('--runtime-assets', required=True)
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
    command('ccache-directory', ccache_directory, builddir=False)
    command('scons-dev-directory', scons_dev_directory, builddir=False)
    command('identity', identity)
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
    sub.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args(argv)
    args.function(args)


if __name__ == '__main__':
    main()
