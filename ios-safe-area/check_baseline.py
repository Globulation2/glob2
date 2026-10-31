import json,pathlib,subprocess
out=pathlib.Path('artifacts/ios-safe-area')
old=out/'HostViewport-before.h'
old.write_bytes(subprocess.check_output(['git','show','HEAD^:libgag/include/HostViewport.h']))
probe=out/'bridge-before.cpp'
probe.write_text((out/'bridge.cpp').read_text().replace('#include <HostViewport.h>','#include "HostViewport-before.h"'))
command=json.loads(next(line[len('COMMAND '):] for line in (out/'ios-compile.log').read_text().splitlines() if line.startswith('COMMAND ')))
command[command.index('-c')+1]=str(probe)
obj=out/'bridge-before.o'
command[command.index('-o')+1]=str(obj)
print('COMMAND',json.dumps(command),flush=True)
subprocess.run(command,check=True)
for filename,expected in [(obj,False),(out/'iphonesimulator-bridge.o',True)]:
    symbols=subprocess.check_output(['nm','-u',str(filename)],text=True)
    for name in ['iosGameSafeInsets','iosGameKeyboardInset','iosGameHasPointer']:
        assert (name in symbols)==expected, (name,symbols)
        print(('FIXED references ' if expected else 'BASELINE omits ')+name)
print('PASS: before/after reproduces and fixes all three disconnected native iOS bridges')
