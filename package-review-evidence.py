"""Create review evidence only after every selected official test passed."""
from pathlib import Path
import gzip
import hashlib
import json
import platform
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
OUT = ROOT / 'publish'
OUT.mkdir(exist_ok=True)
def command(*args):
    return subprocess.check_output(args, cwd=REPO, text=True).strip()
head = command('git','rev-parse','HEAD')
base = command('git','merge-base','HEAD','origin/master')
assert not command('git','status','--porcelain'), 'Source working tree must be clean'
results = {}
for codec in ('h264','vp9','av1'):
    results[codec] = {}
    for kind in ('unit','engine'):
        report = ROOT / ('official-'+codec) / (kind+'.xml')
        tree = ET.parse(report)
        cases = list(tree.iter('testcase'))
        assert cases, report
        assert not list(tree.iter('failure')) and not list(tree.iter('error')), report
        proofs = list((ROOT/('official-'+codec)/kind).rglob('build-provenance.json'))
        assert proofs, (codec,kind)
        for proof in proofs:
            stamp = json.loads(proof.read_text())
            assert stamp['revision'] == head and not stamp['dirty'], proof
        results[codec][kind] = {'cases':len(cases), 'skipped':len(list(tree.iter('skipped')))}
    for name in (codec+'-final-full-build.log',):
        assert 'scons: done building targets.' in (ROOT/name).read_text(), name
    log = ROOT/(codec+'-official-tool-tests.log')
    assert '\nOK\n' in log.read_text(), log

receipts = {}
for codec in results:
    prefix = ROOT/(codec+'-reviewed')/'prefix'
    receipt = json.loads((prefix/'recording-manifest.json').read_text())
    receipts[codec] = {'manifest':receipt, 'staticArchiveBytes':{p.name:p.stat().st_size for p in (prefix/'lib').glob('*.a')}}
    target = OUT/'receipts'/codec
    shutil.copytree(prefix/'share/licenses/recording',target,dirs_exist_ok=True)
    shutil.copyfile(prefix/'recording-manifest.json',target/'recording-manifest.json')

metadata = {'sourceRevision':head,'recordedBase':base,'platform':platform.platform(),
            'compiler':command('c++','--version').splitlines()[0],
            'ffmpeg':command('ffmpeg','-version').splitlines()[0],
            'results':results,'receipts':receipts,
            'sdl':json.loads((ROOT/'native-sdl/prefix/sdl3-manifest.json').read_text())}
(OUT/'verification.json').write_text(json.dumps(metadata,indent=2)+'\n')

# Logs compress individually; recordings and checksums remain together per profile.
for source in sorted(ROOT.glob('*.log')):
    if source.name.startswith(('final-','review-build-system-pinned')) or '-final-full-build' in source.name or '-official-tool-tests' in source.name:
        with source.open('rb') as reader, gzip.open(OUT/(source.name+'.gz'),'wb') as writer:
            shutil.copyfileobj(reader,writer)
for codec in results:
    source = ROOT/('official-'+codec)
    with tarfile.open(OUT/(codec+'-official-evidence.tar.gz'),'w:gz') as package:
        for path in sorted(source.rglob('*')):
            if path.is_file() and path.suffix not in ('.pcm','.raw'):
                package.add(path,arcname=str(Path(codec)/path.relative_to(source)))
for name in ('run-official-profile.py','finish-profile-checks.py','verify-checksum-evidence.py','package-review-evidence.py'):
    shutil.copyfile(ROOT/name,OUT/name)
checksums = {str(path.relative_to(OUT)):hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(OUT.rglob('*')) if path.is_file() and path.name != 'SHA256SUMS'}
(OUT/'SHA256SUMS').write_text(''.join(value+'  '+name+'\n' for name,value in checksums.items()))
print(json.dumps({'head':head,'results':results,'bytes':sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())},indent=2))
