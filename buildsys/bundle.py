"""The macOS application bundle: Glob2.app with its assets, libraries and dSYM.

The CMake build's bundle target runs createBundle through
buildsys/cmake_generate.py bundle, with the environment bundle_environment builds.
"""
import os
import sys
import shutil
import subprocess
from pathlib import Path
sys.path.append( os.path.dirname(__file__) )
from addDependentLibsToBundle import addDependentLibsToBundle
from moltenvk import install_macos
from apple_localizations import copy_localizations

# Bundle inputs beside the build directory's glob2, relative to the checkout.
RESOURCE_DIRECTORIES = ["data", "maps", "campaigns", "scripts",
                        "platforms/macos/en.lproj", "platforms/macos/mr.lproj", "platforms/macos/ta.lproj",
                        "platforms/macos/te.lproj", "platforms/macos/bg.lproj", "platforms/macos/th.lproj"]
PLIST = "platforms/macos/Info.plist"
ICON = "platforms/macos/Glob2.icns"


def _flatten(items):
    if isinstance(items, (list, tuple)):
        return [leaf for item in items for leaf in _flatten(item)]
    return [items]


def run(command) :
    print("\033[32m:: " + command + "\033[0m")
    result = os.system(command)
    if result:
        raise RuntimeError("bundle command failed: %s" % command)
    return result
def norun(command) :
    print("\033[31mXX " + command + "\033[0m")



def createBundle(target, source, env) :
    bundleDir = env['BUNDLE_NAME']+'.app'
    run("rm -rf "+bundleDir )
    run("mkdir -p %s/Contents/Resources" % bundleDir )
    run("mkdir -p %s/Contents/Frameworks" % bundleDir )
    run("mkdir -p %s/Contents/MacOS" % bundleDir )
    if not env.get('LEAN_IMAGE_PREFIX'):
        raise RuntimeError('Mac release packaging requires the pinned lean SDL_image build')
    # add binaries
    for bin in _flatten(env['BUNDLE_BINARIES']):
        original = str(bin)
        packaged = str(Path(bundleDir)/'Contents/MacOS'/Path(original).name)
        shutil.copy2(original, packaged)
        symbols = Path(env['BUNDLE_SYMBOL_DIR'])/(Path(original).name+'.dSYM')
        symbols.parent.mkdir(parents=True, exist_ok=True)
        def uuid(path):
            return {line.split()[1] for line in subprocess.check_output(['dwarfdump', '--uuid', str(path)], text=True).splitlines() if line.startswith('UUID:')}
        original_uuid = uuid(original)
        if not symbols.exists() or original_uuid != uuid(symbols):
            if symbols.exists(): shutil.rmtree(symbols)
            subprocess.run(['dsymutil', original, '-o', str(symbols)], check=True)
        if not original_uuid or original_uuid != uuid(symbols):
            raise RuntimeError('Release dSYM UUID does not match executable')
        subprocess.run(['strip', '-S', '-x', packaged], check=True)
    # Dynamic compute dependencies cannot be discovered through Mach-O linkage.
    install_macos(bundleDir, env['BUILDDIR'])
    # Exported image bytes and source notices are the only runtime resources.
    from tools.package_assets import export_assets
    assets = Path(env['BUILDDIR'])/'runtime-assets'
    from runtime_assets import sprite_packing_profile
    packing = sprite_packing_profile(env.get('sprite_packing', 'auto'), env.get('release', True),
                                     'GLOB2_H264_ASSETS' in env.get('CPPDEFINES', []))
    export_assets(Path.cwd(), assets, platform='macos', jobs=env['BUNDLE_JOBS'],
                  terrain_sheets=packing != 'legacy', packed_hd_sprites=packing != 'legacy',
                  video_sprites=packing == 'video')
    for directory in ('data', 'maps', 'campaigns', 'scripts'):
        if (assets/directory).is_dir():
            shutil.copytree(assets/directory, Path(bundleDir)/'Contents/Resources'/directory)
    copy_localizations(env.get('BUNDLE_RESOURCEDIRS', []), Path(bundleDir)/'Contents/Resources')
    if env.get('RECORDING_PREFIX'):
        shutil.copytree(Path(env['RECORDING_PREFIX'])/'share/licenses/recording',Path(bundleDir)/'Contents/Resources/licenses/recording',dirs_exist_ok=True)
    run('cp COPYING %s/Contents/Resources/' % bundleDir)
    run('cp content/data/javascript-licenses.txt %s/Contents/Resources/' % bundleDir)
    run('cp content/data/json-license.txt %s/Contents/Resources/' % bundleDir)
    run('cp content/data/etcpak-license.txt %s/Contents/Resources/' % bundleDir)
    run('cp docs/assets/source-attribution.md %s/Contents/Resources/' % bundleDir)
    # write Info.plist -- TODO actually write it not copy it
    plistFile = env['BUNDLE_PLIST']
    run('cp %s %s/Contents/Info.plist' % (plistFile, bundleDir) )
    run('/usr/libexec/PlistBuddy -c "Set :CFBundleVersion %s" %s/Contents/Info.plist' % (env['VERSION'], bundleDir))
    run('/usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string %s" %s/Contents/Info.plist' % (env['VERSION'], bundleDir))
    # add icon -- TODO generate .icns file from png or svg
    iconFile = env['BUNDLE_ICON']
    run('cp %s %s/Contents/Resources' % (iconFile, bundleDir) )
    # add dependent libraries, fixing all absolute paths and re-signing everything
    # install_name_tool touched (it invalidates a Mach-O's existing signature, and
    # macOS refuses to run a binary carrying one)
    addDependentLibsToBundle( bundleDir )
    # the bundle's own seal (Info.plist, resources) still needs signing even though
    # every binary inside it already was
    run("codesign --force --sign - %s" % bundleDir )


def bundle_environment(builddir, binary, lean_image_prefix, recording_prefix, version, jobs,
                       sprite_packing='auto', h264=True):
    """The settings createBundle reads, for a build directory."""
    builddir = str(builddir)
    return {'BUNDLE_NAME': builddir + '/Glob2', 'BUNDLE_SYMBOL_DIR': builddir + '/symbols',
            'BUNDLE_BINARIES': [str(binary)], 'BUNDLE_RESOURCEDIRS': list(RESOURCE_DIRECTORIES),
            'BUNDLE_PLIST': PLIST, 'BUNDLE_ICON': ICON, 'BUILDDIR': builddir,
            'LEAN_IMAGE_PREFIX': str(lean_image_prefix), 'RECORDING_PREFIX': str(recording_prefix or ''),
            'VERSION': version, 'BUNDLE_JOBS': jobs, 'sprite_packing': sprite_packing, 'release': True,
            'CPPDEFINES': ['GLOB2_H264_ASSETS'] if h264 else []}

