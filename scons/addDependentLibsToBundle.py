#!/usr/bin/env python

import os, glob
import sys

def run(command) :
    print(("\033[32m:: ", command, "\033[0m"))
    return os.system(command)
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
    if entry in visited : return
    visited.append( entry )
    real = entry
    if not os.path.isabs(entry) :
        real = resolveByBasename(entry, searchDirs)
        if real is None :
            norun("could not resolve %s to a real file (searched Homebrew lib dirs)" % entry)
            return
    if not needsChange( real, blacklist ) : return
    resolved[entry] = real
    for line in os.popen("otool -L "+real).readlines()[1:] :
        dep = line.split()[0]
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
        for line in os.popen("otool -L "+binary).readlines()[1:] :
            libDependencies(line.split()[0], resolved, visited, doNotChange, searchDirs)

    libs = sorted(set( (os.path.basename(real), real) for real in resolved.values() ))
    run("mkdir -p %(bundle)s/Contents/Frameworks/" % locals() )

    # copy every dependent lib into the bundle once and set its own id to its bundled path
    for lib, path in libs :
        run("cp %(path)s %(bundle)s/Contents/Frameworks/%(lib)s" % locals() )
        run("chmod u+w %(bundle)s/Contents/Frameworks/%(lib)s" % locals() )
        run("install_name_tool -id @executable_path/../Frameworks/%(lib)s %(bundle)s/Contents/Frameworks/%(lib)s" % locals() )
    # fix every reference any binary or bundled lib made to a dependency, however it
    # originally named it (absolute path, @rpath/, or @loader_path/), to point at the
    # one bundled copy
    frameworkFiles = [os.path.join(bundle, "Contents", "Frameworks", lib) for lib, _ in libs]
    for current in binaries + frameworkFiles :
        for entry, real in resolved.items() :
            lib = os.path.basename(real)
            run("install_name_tool -change %(entry)s @executable_path/../Frameworks/%(lib)s %(current)s" % locals() )
    # install_name_tool invalidates whatever signature a Homebrew-built dylib or this
    # project's own binary shipped with, and macOS kills any process holding a page
    # with an invalid signature — sign the bundled libs first, then each binary, so
    # nothing in the bundle is left with a stale signature at any point.
    for current in frameworkFiles + binaries :
        run("codesign --force --sign - %(current)s" % locals() )

if __name__ == "__main__":
    addDependentLibsToBundle( "Annotator.app" )
