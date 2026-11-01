from pathlib import Path
import json,xml.etree.ElementTree as ET,time
path=Path(__file__).with_name('focused_pipeline.py');exec(path.read_text().split('p=argparse.ArgumentParser();')[0])
assert git('rev-parse','HEAD')=='cdd634cebcde401f1266b6739f8ea21094eb4e56' and not git('status','--porcelain')
assert git('diff','--name-only','e4811dcde89d6393918c36a99d34242ef3be8178','HEAD').splitlines()==['test/fixtures/multiplayer/FourSquares1.g2mr']
assert json.loads((BASE/'focused-link-command.json').read_text())['exit_code']==0
end=time.monotonic()+1800
while not (BASE/'explicit-native-cli-build-command.json').exists():
 assert time.monotonic()<end;time.sleep(2)
assert json.loads((BASE/'explicit-native-cli-build-command.json').read_text())['exit_code']==0
# Emit the authoritative current native full golden trace first.
output=BASE/'native-match-verify';assert not output.exists()
run('native-match-verify',[str(BUILD/'src/glob2'),'match','verify',str(ROOT/GOLDENS[0]),'--map-file',str(ROOT/'maps/FourSquares1.map.gz'),'--output-dir',str(output)],600,video='dummy')
verdict=json.loads((output/'verdict.json').read_text());assert verdict['verdict']=='verified'
assert (output/'checksums.txt').read_bytes()==(ROOT/GOLDENS[1]).read_bytes()
write('authoritative-native-trace.json',{'compiled_revision':'e4811dcde89d6393918c36a99d34242ef3be8178','fixture_commit':git('rev-parse','HEAD'),'actual_binary_sha256':sha(BUILD/'src/glob2'),'record_sha256':sha(ROOT/GOLDENS[0]),'trace_sha256':sha(output/'checksums.txt'),'trace_lines':len((output/'checksums.txt').read_bytes().splitlines()),'verdict':verdict,'production_carry':'git diff e481..cdd changes only committed .g2mr fixture; all shipping code/data/font/catalog inputs identical.'})
print('Authoritative native CLI trace complete.',flush=True)
