import json, pathlib, subprocess, sys
root=pathlib.Path.cwd()
sys.path.insert(0,str(root/'scons'))
from sources import INCLUDE_DIRECTORIES
from client_features import header
out=root/'artifacts/ios-safe-area'
config=out/'include/glob2/BuildConfig.h'
config.parent.mkdir(parents=True,exist_ok=True)
config.write_text('''#pragma once
#define PACKAGE "glob2"
#define PACKAGE_NAME "Globulation 2"
#define PACKAGE_VERSION "0.9.5"
#define PACKAGE_DATA_DIR "."
#define PACKAGE_SOURCE_DIR "."
#define PRIMARY_FONT "sans.ttf"
#define GLOB2_MOBILE 1
#define GLOB2_NATIVE_WSS 1
#define GLOB2_NO_VOICE 1
'''+header({}))
probe=out/'bridge.cpp'
probe.write_text('''#include <HostViewport.h>
#ifndef SDL_PLATFORM_IOS
#error Expected SDL3 iOS platform detection
#endif
#ifdef __IPHONEOS__
#error Old SDL2 platform macro unexpectedly defined
#endif
GAGCore::SafeInsets safe(GAGCore::GraphicContext* gfx) { return GAGCore::mobileSafeInsets(gfx); }
double keyboard(GAGCore::GraphicContext* gfx) { return GAGCore::mobileKeyboardInset(gfx); }
bool pointer() { return GAGCore::mobilePointerAvailable(); }
''')
for sdk, suffix, prefix in [('iphonesimulator','-simulator','Darwin-arm64-bf7fd708466fb87b8f687610'),('iphoneos','','Darwin-arm64-73842f193f58cdbd8a88199d')]:
    deps=pathlib.Path.home()/'Library/Application Support/Glob2/Development/dependencies'/prefix
    sysroot=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
    flags=['-target','arm64-apple-ios15.0'+suffix,'-isysroot',sysroot,'-std=gnu++20','-DHAVE_CONFIG_H','-I'+str(out/'include'),'-I'+str(deps/'include'),'-I'+str(deps/'include/opus'),'-I'+str(deps/'include/SDL3'),'-Ithird_party/quickjs-ng']+['-I'+p for p in INCLUDE_DIRECTORIES]
    for name in [str(probe.relative_to(root)),'libgag/src/GraphicContextMobile.cpp','libgag/src/ui/Presentation.cpp','src/hud/touch/GameGUITouch.cpp','mobile/ios/SafeArea.mm']:
        obj=out/(sdk+'-'+pathlib.Path(name).stem+'.o')
        cmd=['xcrun','--sdk',sdk,'clang++',*flags,'-c',name,'-o',str(obj)]
        print('COMMAND',json.dumps(cmd),flush=True)
        subprocess.run(cmd,check=True)
        print('PASS',sdk,name,flush=True)
    symbols=subprocess.check_output(['nm','-u',str(out/(sdk+'-bridge.o'))],text=True)
    for expected in ['iosGameSafeInsets','iosGameKeyboardInset','iosGameHasPointer']:
        assert expected in symbols, (expected,symbols)
    print('PASS: actual HostViewport object references all 3 native iOS bridge functions',flush=True)
print('PASS: 10 translation units compiled for simulator/device; 6 native bridge reference checks')
