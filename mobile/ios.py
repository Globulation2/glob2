#!/usr/bin/env python3
"""Generate an Xcode app project; SCons compiles the shared game core."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scons'))
from build_layout import build_identity, default_directory, write_if_changed
from mobile_toolchain import ROOT, discover
from sources import INCLUDE_DIRECTORIES


def cmake_quote(value):
    return '"'+str(value).replace('\\','/').replace('"','\\"').replace('$','\\$').replace(';','\\;')+'"'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['configure','build','install','launch'])
    parser.add_argument('--environment',default='simulator',choices=['device','simulator'])
    parser.add_argument('--release',action='store_true')
    parser.add_argument('--developer-dir')
    signing=parser.add_mutually_exclusive_group()
    signing.add_argument('--team',help='Local Apple development team for device signing')
    signing.add_argument('--unsigned',action='store_true',help='Build an unsigned device app without credentials (not installable)')
    parser.add_argument('--device',help='Exact simulator or device identifier for install/launch')
    parser.add_argument('--simulator-set',default=str(ROOT/'build/mobile-tools/ios-simulators'),
                        help='Isolated simulator device set for install/launch')
    parser.add_argument('--cmake',default='cmake')
    args=parser.parse_args()
    if args.command=='install' and args.environment=='device' and args.unsigned:
        raise ValueError('An unsigned iOS device app cannot be installed; rebuild with --team and a local signing identity/profile')
    # Reject incomplete requests before probing SDKs or compiling the core.
    if args.command in ('configure','build') and args.environment=='device' and not (args.team or args.unsigned):
        raise ValueError('Device packaging needs --team with a local signing identity/profile, or --unsigned for compilation without credentials')
    if args.command in ('install','launch') and not args.device:
        raise ValueError('--device is required; never choose an arbitrary device')
    options={'target':'ios','environment':args.environment,'release':int(args.release)}
    if args.developer_dir: options['developer_dir']=args.developer_dir
    identity=build_identity(options);discover(identity,options)
    output=ROOT/default_directory(identity)
    project=output/'xcode-project';configuration='Release' if args.release else 'Debug'
    sdk='iphonesimulator' if args.environment=='simulator' else 'iphoneos'
    app=project/'build'/(configuration+'-'+sdk)/'Glob2.app'
    (output/'tmp').mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,TMPDIR=str(output/'tmp'),TMP=str(output/'tmp'),TEMP=str(output/'tmp'))
    if args.developer_dir: env['DEVELOPER_DIR']=args.developer_dir
    if args.command in ('install','launch'):
        if args.environment=='simulator':
            command=['xcrun','simctl','--set',str(Path(args.simulator_set).resolve()),args.command,args.device,str(app) if args.command=='install' else 'org.globulation.glob2']
        else:
            command=['xcrun','devicectl','device']+(['install','app','--device',args.device,str(app)] if args.command=='install' else ['process','launch','--device',args.device,'org.globulation.glob2'])
        subprocess.run(command,env=env,check=True);return
    triplet='glob2-arm64-ios'+('-simulator' if args.environment=='simulator' else '')
    prefix=output/'vcpkg-installed'/triplet
    if not (prefix/'manifest.json').exists(): raise ValueError('Build iOS dependencies first with mobile/dependencies.py --target ios --environment '+args.environment)
    scons=shutil.which('scons')
    if not scons: raise ValueError('SCons is required')
    command=[scons]+[str(k)+'='+str(v) for k,v in options.items()]+['mobile_deps='+str(prefix),'-j8']
    subprocess.run(command,cwd=ROOT,env=env,check=True)
    project.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((prefix/'manifest.json').read_text())
    include=[output/'include',prefix/'include',prefix/'include/SDL2']+[ROOT/p for p in INCLUDE_DIRECTORIES]
    libraries=[output/'lib/libglob2.a']+[prefix/p for p in manifest['archives']]
    lines=['cmake_minimum_required(VERSION 3.24)','project(Glob2 LANGUAGES C CXX OBJC OBJCXX)',
        'set(CMAKE_CXX_STANDARD 20)', 'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
        'add_executable(Glob2 MACOSX_BUNDLE '+cmake_quote(ROOT/'src/Glob2.cpp')+')',
        'target_compile_definitions(Glob2 PRIVATE HAVE_CONFIG_H)',
        'target_include_directories(Glob2 PRIVATE '+' '.join(map(cmake_quote,include))+')',
        'add_custom_target(Glob2Core COMMAND '+' '.join(map(cmake_quote,command))+' WORKING_DIRECTORY '+cmake_quote(ROOT)+' VERBATIM)',
        'add_dependencies(Glob2 Glob2Core)',
        'target_link_libraries(Glob2 PRIVATE '+' '.join(map(cmake_quote,libraries))+')',
        'target_link_options(Glob2 PRIVATE -ObjC)',
        'set_target_properties(Glob2 PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.globulation.glob2 XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS YES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")',
        'set_target_properties(Glob2 PROPERTIES MACOSX_BUNDLE_INFO_PLIST '+cmake_quote(ROOT/'mobile/ios/Info.plist.in')+' XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")']
    for framework in ('UniformTypeIdentifiers','UIKit','Foundation','AudioToolbox','CoreAudio','AVFoundation','CoreGraphics','CoreHaptics','CoreMotion','CoreBluetooth','GameController','Metal','QuartzCore','OpenGLES','Security','SystemConfiguration'):
        lines.append('target_link_libraries(Glob2 PRIVATE "-framework '+framework+'")')
    for folder in ('data','maps','campaigns','scripts'):
        for resource in sorted((ROOT/folder).rglob('*')):
            if resource.is_file():
                lines.append('target_sources(Glob2 PRIVATE '+cmake_quote(resource)+')')
                lines.append('set_source_files_properties('+cmake_quote(resource)+' PROPERTIES MACOSX_PACKAGE_LOCATION '+cmake_quote(resource.relative_to(ROOT).parent)+')')
    write_if_changed(project/'CMakeLists.txt','\n'.join(lines)+'\n')
    configure=[args.cmake,'-G','Xcode','-S',str(project),'-B',str(project/'build'),'-DCMAKE_SYSTEM_NAME=iOS',
        '-DCMAKE_XCODE_ATTRIBUTE_CACHE_ROOT='+str(output/'xcode-cache'),
        '-DCMAKE_XCODE_ATTRIBUTE_SDK_STAT_CACHE_DIR='+str(output/'xcode-cache'),
        '-DCMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS=YES',
        '-DCMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT=dwarf-with-dsym',
        '-DCMAKE_CONFIGURATION_TYPES='+configuration,'-DCMAKE_OSX_SYSROOT='+sdk,'-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0']
    if args.environment=='simulator' or args.unsigned:
        configure+=['-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO','-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=']
    elif args.team:
        configure+=['-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=YES','-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM='+args.team]
    else: raise ValueError('Device packaging needs --team with a local signing identity/profile, or --unsigned for compilation without credentials')
    subprocess.run(configure,env=env,check=True)
    print('Xcode project:',project/'build/Glob2.xcodeproj')
    if args.command=='build': subprocess.run([args.cmake,'--build',str(project/'build'),'--config',configuration],env=env,check=True)

if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        print(error,file=sys.stderr);sys.exit(1)
