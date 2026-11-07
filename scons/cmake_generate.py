#!/usr/bin/env python3
"""Command-line entry points through which the experimental CMake build calls
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
    print((ROOT / default_directory(build_identity({'dev_fast': 1, 'linker': 'auto'}))).as_posix())


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
    args = parser.parse_args(argv)
    args.function(args)


if __name__ == '__main__':
    main()
