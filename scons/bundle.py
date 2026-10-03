import SCons.Util, os
import sys
import shutil
import subprocess
from pathlib import Path
sys.path.append( os.path.dirname(__file__) )
from addDependentLibsToBundle import addDependentLibsToBundle

def run(command) :
    print(("\033[32m:: ", command, "\033[0m"))
    result = os.system(command)
    if result:
        raise RuntimeError("bundle command failed: %s" % command)
    return result
def norun(command) :
    print(("\033[31mXX ", command, "\033[0m"))



def createBundle(target, source, env) :
    bundleDir = env['BUNDLE_NAME']+'.app'
    run("rm -rf "+bundleDir )
    run("mkdir -p %s/Contents/Resources" % bundleDir )
    run("mkdir -p %s/Contents/Frameworks" % bundleDir )
    run("mkdir -p %s/Contents/MacOS" % bundleDir )
    if not env.get('LEAN_IMAGE_PREFIX'):
        raise RuntimeError('Mac release packaging requires the pinned lean SDL_image build')
    # add binaries
    for bin in env.Flatten( env['BUNDLE_BINARIES'] ) :
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
    # Exported image bytes and source notices are the only runtime resources.
    from tools.package_assets import export_assets
    assets = Path(env['BUILDDIR'])/'runtime-assets'
    export_assets(Path.cwd(), assets, platform='macos')
    for directory in ('data', 'maps', 'campaigns', 'scripts'):
        if (assets/directory).is_dir():
            shutil.copytree(assets/directory, Path(bundleDir)/'Contents/Resources'/directory)
    if env.get('RECORDING_PREFIX'):
        shutil.copytree(Path(env['RECORDING_PREFIX'])/'share/licenses/recording',Path(bundleDir)/'Contents/Resources/licenses/recording',dirs_exist_ok=True)
    run('cp COPYING %s/Contents/Resources/' % bundleDir)
    run('cp data/javascript-licenses.txt %s/Contents/Resources/' % bundleDir)
    run('cp data/json-license.txt %s/Contents/Resources/' % bundleDir)
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


def createBundleMessage(target, source, env) :
    out ="Running Bundle builder\n"
    for a in target : out+= "Target:"+ str(a) + "\n"
    for a in source : out+= "Source:"+ str(a) + "\n"
    return out

def bundleEmitter(target, source, env):
    target = env.Dir(env['BUNDLE_NAME']+".app")
    source = env['BUNDLE_BINARIES']
    return target, source

def generate(env) :
    print("Loading Bundle tool")
    Builder = SCons.Builder.Builder
    Action = SCons.Action.Action
    bundleBuilder = Builder(
        action = Action( createBundle, createBundleMessage ),
        emitter = bundleEmitter,
    )
    env['BUNDLE_RESOURCEDIRS'] = []
    env['BUNDLE_PLUGINS'] = []
    env.Append( BUILDERS={'Bundle' : bundleBuilder } )

def exists(env) :
    return True
