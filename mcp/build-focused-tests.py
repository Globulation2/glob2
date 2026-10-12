from pathlib import Path
import shlex, subprocess, os, json
root=Path(__file__).resolve().parents[2]
selected={
 'test/support/TestMain.cpp','test/support/Glob2Test.cpp','test/support/GlobalContainerSlot.cpp',
 'test/support/EngineFixtures.cpp','test/support/LegacyResourceTypes.cpp',
 'src/mcp/MCPTest.cpp','src/ui/settings/SettingsScreenTest.cpp','src/ui/settings/SettingsGraphicsTest.cpp',
 'src/ui/settings/SettingsExperimentsTest.cpp','src/render/RendererSwitchIntegrationTest.cpp',
 'libgag/src/WindowResizeHarness.cpp','libgag/src/PortableRendererHarness.cpp','src/render/SoftwareRendererTest.cpp',
 'test/GameplayRecordingIntegrationTest.cpp','src/hud/GUIInteractionCoverageTest.cpp','src/hud/GUIOrderCoverageTest.cpp','src/hud/touch/GameGUITouchHarness.cpp',
 'test/SavegameSafetyHarness.cpp','src/engine/EngineSessionHarness.cpp',
 'src/net/turn/TurnEngineHarness.cpp','src/map/editor/EditorFlowsTest.cpp',
 'src/map/editor/EditorActionCoverageTest.cpp','src/map/editor/EditorTerrainPaintTest.cpp'}
plan=Path('artifacts/mcp/test-plan.log').read_text().splitlines()
objects=[]
for line in plan:
 if ' -c ' not in line: continue
 args=shlex.split(line)
 if args[-1] in selected and '-o' in args: objects.append(args[args.index('-o')+1])
assert len(objects)==len(selected),(len(objects),len(selected))
env=dict(os.environ,GLOB2_GLSLANG=str(root/'artifacts/mcp/glslang-build/StandAlone/glslang'),GLOB2_SDL3_PREFIX='/home/bradley/glob2/build/sdl3/prefix')
command=['python3','tools/dev_build.py','--build=build/mcp-dev','-j6','build/mcp-dev/src/glob2',*objects]
Path('artifacts/mcp/focused-build-command.json').write_text(json.dumps(command,indent=2))
with open('artifacts/mcp/final-build.log','w') as log:
 subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
link=next(shlex.split(line) for line in plan if line.startswith('g++ -o build/mcp-dev/test/glob2-engine-tests '))
link=[arg.replace('-Lbuild/sdl3/prefix/lib', '-L/home/bradley/glob2/build/sdl3/prefix/lib') for arg in link if not (arg.startswith('build/mcp-dev/test/engine-') and arg.endswith('.o') and arg not in objects)]
link.insert(3, 'build/mcp-dev/src/ui/settings/SettingsScreenMCP.o')
link.insert(3, 'build/mcp-dev/src/ui/screens/HandPreferenceScreen.o')
# Compile the changed screen-dispatch unit harness against production libraries too.
screen_command=next(shlex.split(line) for line in plan if ' -c ' in line and line.endswith('libgag/src/WindowResizeHarness.cpp'))
screen_command[screen_command.index('-o')+1]='artifacts/mcp/focused-build/ScreenExecutionHarness.o'
screen_command[-1]='libgag/src/ScreenExecutionHarness.cpp'
screen_command=[arg.replace('-Ibuild/sdl3/prefix/include', '-I/home/bradley/glob2/build/sdl3/prefix/include') for arg in screen_command]
Path('artifacts/mcp/focused-build').mkdir(parents=True,exist_ok=True)
with open('artifacts/mcp/screen-harness-build.log','w') as log:
 subprocess.run(screen_command,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
link.insert(3, 'artifacts/mcp/focused-build/ScreenExecutionHarness.o')
out=Path('artifacts/mcp/focused-build/test/glob2-engine-tests');out.parent.mkdir(parents=True,exist_ok=True)
link[link.index('-o')+1]=str(out)
Path('artifacts/mcp/focused-link-command.json').write_text(json.dumps(link,indent=2))
with open('artifacts/mcp/focused-link.log','w') as log: subprocess.run(link,stdout=log,stderr=subprocess.STDOUT,check=True,env=env)
print('Focused tests and desktop game built successfully',flush=True)
