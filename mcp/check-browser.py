import json, subprocess, shlex, os
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39')
compiler=sdk/'upstream/emscripten/em++'
config=Path('/home/bradley/glob2')/'build/emscripten/client/debug/dev-dev_fast-true-linker-auto-web_variant-threaded/direct/threaded'
rows=json.load(open('build/mcp-dev/compile_commands.json'))
files=['src/mcp/UIService.cpp','src/app/Application.cpp','src/app/Settings.cpp','src/ui/settings/SettingsScreen.cpp','src/ui/settings/SettingsScreenMCP.cpp','libgag/src/ScreenStack.cpp','libgag/src/GraphicContextRenderer.cpp','src/hud/touch/GameGUITouch.cpp','src/hud/input/GameGUIInput.cpp','src/hud/GameGUIStep.cpp','src/game/screens/GameSessionScreen.cpp','src/map/editor/screens/MapEditorScreen.cpp','src/map/editor/MapEditIO.cpp','src/engine/EngineRun.cpp']
with open('artifacts/mcp/browser-compile.log','w') as log:
 for source in files:
  row=next((r for r in rows if r['file']==source),None)
  if row is None:
   row=next(r for r in rows if r['file']=='src/ui/settings/SettingsScreen.cpp')
   row=dict(row,command=row['command'].replace('src/ui/settings/SettingsScreen.cpp',source))
  args=shlex.split(row['command']);args=args[args.index('g++')+1:]
  i=args.index('-o');del args[i:i+2];args[args.index('-c')]='-fsyntax-only'
  args=[a.replace('-Ibuild/sdl3/prefix/include','-I/home/bradley/glob2/build/sdl3/prefix/include') for a in args if a!='-Ibuild/mcp-dev/include']
  args=[str(compiler),'-I'+str(root/'artifacts/mcp/browser-include'),*args]
  env=dict(os.environ, EM_CONFIG=str(config/'.emscripten')) if (config/'.emscripten').exists() else dict(os.environ)
  result=subprocess.run(args,text=True,stdout=log,stderr=subprocess.STDOUT,env=env)
  print(source,result.returncode,flush=True)
  if result.returncode: raise SystemExit(result.returncode)
