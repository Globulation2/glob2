from pathlib import Path
import subprocess,shlex,json
names=['src/app/Glob2.cpp','src/app/GlobalContainerArgs.cpp','src/app/cli/CommandLine.cpp','src/app/cli/Headless.cpp','src/app/cli/MapCommand.cpp','src/app/cli/MapStudy.cpp','src/online/InviteLink.cpp','src/hive/HiveClient.cpp','mobile/MobilePaths.cpp','src/app/cli/RenderSkin.cpp','src/app/cli/VerifyMatch.cpp','src/app/cli/TurnClientCommand.cpp','src/scripting/javascript/ScriptCommand.cpp','libgag/src/FileManager.cpp']
lines=Path('artifacts/cli/android-config.log').read_text().splitlines();results=[]
for name in names:
 matches=[line for line in lines if line.startswith('/usr/bin/ccache ') and line.endswith(' '+name)]
 if not matches:results.append({'source':name,'error':'No compile command'});continue
 args=shlex.split(matches[0])[1:];args.remove('-c');index=args.index('-o');del args[index:index+2];args.append('-fsyntax-only')
 p=subprocess.run(args,capture_output=True,text=True,timeout=120)
 Path('artifacts/cli/android-'+Path(name).stem+'.log').write_text(p.stdout+p.stderr);results.append({'source':name,'exit_code':p.returncode})
Path('artifacts/cli/android-syntax.json').write_text(json.dumps(results,indent=2));print(results)
raise SystemExit(any(r.get('exit_code',1)!=0 for r in results))
