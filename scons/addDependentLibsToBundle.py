#!/usr/bin/env python

import os, glob
import sys
import shutil
import subprocess

def run(command) :
    print(("\033[32m:: ", command, "\033[0m"))
    result = os.system(command)
    if result:
        raise RuntimeError("bundle command failed: %s" % command)
    return result
def norun(command) :
    print(("\033[31mXX ", command, "\033[0m"))

# Where a Homebrew-built dylib lives when its own load commands name it by a bare
# @rpath/ or @loader_path/ entry instead of an absolute path (boost and libwebp's
# transitive deps do this): every Cellar package's lib/ dir, plus the linked-keg-only
# /opt/homebrew/lib itself.
def homebrewLibDirs() :
    dirs = ["/opt/homebrew/lib"]
    cellar = "/opt/homebrew/Cellar"
    for pkg in (os.listdir(cellar) if os.path.isdir(cellar) else []) :
        pkgDir = os.path.join(cellar, pkg)
        for ver in (os.listdir(pkgDir) if os.path.isdir(pkgDir) else []) :
            libDir = os.path.join(pkgDir, ver, "lib")
            if os.path.isdir(libDir) :
                dirs.append(libDir)
    return dirs

def resolveByBasename(entry, searchDirs) :
    base = os.path.basename(entry)
    for d in searchDirs :
        candidate = os.path.join(d, base)
        if os.path.isfile(candidate) :
            return candidate
    return None

def needsChange(binary, blacklist) :
    #with python2.5 we could just return all([not binary.startswith(blacksheep) for blacksheep in blacklist])
    for blacksheep in blacklist :
        if binary.startswith( blacksheep ) :
#           print "found blackseep", binary
            return False
    return True

# Resolves `entry` (an absolute path, or a bare @rpath//@loader_path/ reference that
# otool -L cannot itself be run on) to a real file, recurses into that file's own
# dependencies, and records path -> real file so every reference to the same library
# resolves to one bundled copy regardless of which form named it.
def libDependencies(entry, resolved, visited, blacklist, searchDirs) :
    if not needsChange(entry, blacklist) or entry in visited : return
    visited.append( entry )
    real = entry
    if not os.path.isabs(entry) :
        real = resolveByBasename(entry, searchDirs)
        if real is None :
            raise RuntimeError("Could not resolve bundled dependency: " + entry)
    if not needsChange( real, blacklist ) : return
    real = os.path.realpath(real)
    resolved[entry] = real
    for line in subprocess.check_output(["otool", "-L", real], text=True).splitlines()[1:] :
        dep = line.strip().split(" (compatibility version", 1)[0]
        if dep == real or dep == entry : continue
        libDependencies(dep, resolved, visited, blacklist, searchDirs)

def addDependentLibsToBundle( bundle ) :
    binaries = glob.glob(bundle+"/Contents/MacOS/*")
    binaries += glob.glob(bundle+"/Contents/plugins/*")
    doNotChange = [
        "/System/",
        "/usr/lib/",
        "@executable_path/",
    ]
    searchDirs = homebrewLibDirs()
    # entry (as it appears in some binary's load commands) -> real file to copy from
    resolved = {}
    visited = []
    for binary in binaries :
        for line in subprocess.check_output(["otool", "-L", binary], text=True).splitlines()[1:] :
            libDependencies(line.strip().split(" (compatibility version", 1)[0], resolved, visited, doNotChange, searchDirs)

    # Some bundled libraries dlopen() another one at runtime instead of declaring it
    # as a normal linked dependency, so the otool -L walk above can never see it:
    # Homebrew's sdl2 is sdl2-compat, a shim that wraps SDL3 and dlopen()s
    # libSDL3.dylib (by @loader_path/@executable_path-relative name) the first time
    # SDL initializes. Without SDL3 bundled alongside, that lookup fails and
    # sdl2-compat aborts before glob2's own code ever runs.
    runtimeDlopenDeps = {
        "libSDL2-2.0.0.dylib": ["libSDL3.dylib"],
    }
    runtimeAliases = {}
    for real in list(resolved.values()) :
        for dep in runtimeDlopenDeps.get(os.path.basename(real), []) :
            libDependencies(dep, resolved, visited, doNotChange, searchDirs)
            if dep not in resolved:
                raise RuntimeError("Required runtime dependency unavailable: " + dep)
            runtimeAliases[dep] = os.path.basename(resolved[dep])

    libs = sorted(set( (os.path.basename(real), real) for real in resolved.values() ))
    os.makedirs(os.path.join(bundle, "Contents", "Frameworks"), exist_ok=True)
    names = {}
    for lib, path in libs:
        if lib in names and names[lib] != path:
            raise RuntimeError("Conflicting bundled library name: " + lib)
        names[lib] = path

    # copy every dependent lib into the bundle once and set its own id to its bundled path
    for lib, path in libs :
        destination = os.path.join(bundle, "Contents", "Frameworks", lib)
        shutil.copy2(path, destination)
        os.chmod(destination, os.stat(destination).st_mode | 0o200)
        subprocess.run(["install_name_tool", "-id", "@executable_path/../Frameworks/"+lib, destination], check=True)
    for alias, canonical in runtimeAliases.items():
        if alias != canonical:
            os.symlink(canonical, os.path.join(bundle, "Contents", "Frameworks", alias))
    # fix every reference any binary or bundled lib made to a dependency, however it
    # originally named it (absolute path, @rpath/, or @loader_path/), to point at the
    # one bundled copy
    frameworkFiles = [os.path.join(bundle, "Contents", "Frameworks", lib) for lib, _ in libs]
    for current in binaries + frameworkFiles :
        # Only rewrite load commands actually present in this Mach-O. Applying
        # every known dependency to every file creates thousands of no-op
        # install_name_tool invocations as the Homebrew dependency tree grows.
        entries = [line.strip().split(" (compatibility version", 1)[0] for line in subprocess.check_output(["otool", "-L", current], text=True).splitlines()[1:]]
        for entry in entries :
            real = resolved.get(entry)
            if real is None : continue
            lib = os.path.basename(real)
            subprocess.run(["install_name_tool", "-change", entry, "@executable_path/../Frameworks/"+lib, current], check=True)
    # install_name_tool invalidates whatever signature a Homebrew-built dylib or this
    # project's own binary shipped with, and macOS kills any process holding a page
    # with an invalid signature — sign the bundled libs first, then each binary, so
    # nothing in the bundle is left with a stale signature at any point.
    for current in frameworkFiles + binaries :
        subprocess.run(["codesign", "--force", "--sign", "-", current], check=True)

if __name__ == "__main__":
    addDependentLibsToBundle( "Annotator.app" )
