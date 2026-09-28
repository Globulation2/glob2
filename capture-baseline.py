from pathlib import Path
import shlex, subprocess, shutil, json
root=Path('artifacts/landscape-performance')
out=root/'baseline-objects'
out.mkdir(exist_ok=True)
commands=[shlex.split(line) for line in (root/'build-base.log').read_text().splitlines() if line.startswith('/opt/homebrew/bin/ccache g++')]
template=commands[-1]
record=[]
for name in ['CustomGameScreen', 'GUIMapPreview', 'LandscapePreviewer', 'LandscapePickerScreen', 'CustomGameSetupHarness']:
    target=out/(name+'.o')
    source=('test/' if name=='CustomGameSetupHarness' else 'src/')+name+'.cpp'
    command=template.copy()
    command[command.index('-o')+1]=str(target)
    command[-1]=source
    record.append(command)
    print('Baseline compile:',name,flush=True)
    subprocess.run(command,check=True)
(root/'baseline-compile.json').write_text(json.dumps(record,indent=2))
