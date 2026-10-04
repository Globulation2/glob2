from pathlib import Path
import subprocess, hashlib, json, sys
root=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parent/'source-backup'
files=['libgag/include/TrueTypeFont.h','libgag/src/TrueTypeFont.cpp','src/gui/GameGUIDialog.h','src/gui/GameGUIDialog.cpp']
base='79229b3101c6bd6e0c83abb5ed55579834595785'
if sys.argv[1]=='install':
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()=='a4fad39b4e2a2ee1daf882008654eeed4de4a22c', 'Checkout the original patch revision before reconstructing its stock baseline'
 out.mkdir(exist_ok=True)
 entries=[]
 for name in files:
  current=(root/name).read_bytes()
  committed=subprocess.check_output(['git','show','HEAD:'+name],cwd=root)
  assert current==committed, name+' has uncommitted edits'
  stock=subprocess.check_output(['git','show',base+':'+name],cwd=root)
  (out/name.replace('/','_')).write_bytes(current)
  entries.append({'file':name,'candidate':hashlib.sha256(current).hexdigest(),'baseline':hashlib.sha256(stock).hexdigest()})
 (out/'manifest.json').write_text(json.dumps(entries,indent=2)+'\n')
 for entry in entries:
  (root/entry['file']).write_bytes(subprocess.check_output(['git','show',base+':'+entry['file']],cwd=root))
else:
 entries=json.loads((out/'manifest.json').read_text())
 for e in entries:
  assert hashlib.sha256((root/e['file']).read_bytes()).hexdigest()==e['baseline'], e['file']+' changed during baseline'
 for e in entries:
  saved=(out/e['file'].replace('/','_')).read_bytes()
  assert hashlib.sha256(saved).hexdigest()==e['candidate']
  (root/e['file']).write_bytes(saved)
print(sys.argv[1], len(files), 'production files; baseline',base)
