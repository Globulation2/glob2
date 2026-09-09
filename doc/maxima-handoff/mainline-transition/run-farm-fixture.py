import json,subprocess,shutil,tempfile,hashlib
from pathlib import Path
root=Path('/home/bradley/glob2-maxima-mainline-100k-final');out=root/'output/qualification-100k/farm-behavior-adapted';out.mkdir(exist_ok=False)
with tempfile.TemporaryDirectory(prefix='mx-farm-') as temp:
 d=Path(temp);shutil.copy2('/tmp/MaximaFarmingIntegrationTest-mainline.cpp',d/'MaximaFarmingIntegrationTest.cpp');shutil.copy2(root/'test/MaximaFarmMaintenanceSwitchBehaviorTest.cpp',d/'MaximaFarmMaintenanceSwitchBehaviorTest.cpp')
 args=json.loads((root/'output/qualification-100k/farm-behavior/command.json').read_text())['argv']
 args[args.index(str(root/'test/MaximaFarmMaintenanceSwitchBehaviorTest.cpp'))]=str(d/'MaximaFarmMaintenanceSwitchBehaviorTest.cpp');args[-1]=str(d/'fixture')
 args.insert(1,'-I'+str(root/'test'))
 subprocess.run(args,cwd=root,check=True)
 result=subprocess.run([str(d/'fixture')],cwd=root,capture_output=True,text=True);(out/'native.log').write_text(result.stdout+result.stderr);result.check_returncode()
 (out/'PASS.json').write_text(json.dumps({'passed':True,'protocol_id':json.loads((root/'output/qualification-100k/protocol.json').read_text())['protocol_id'],'scope':'32 farming maintenance behavior cases against frozen engine objects; fixture adapted to mainline global gradient API','adapted_fixture_sha256':hashlib.sha256((d/'MaximaFarmingIntegrationTest.cpp').read_bytes()).hexdigest()},indent=2)+'\n')
 print(result.stdout)
