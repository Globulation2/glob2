from pathlib import Path
import subprocess, os, json, tempfile, xml.etree.ElementTree as ET, hashlib
root=Path.cwd();out=root/'artifacts/gradient-merge'
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
base=subprocess.check_output(['git','rev-parse','origin/master'],text=True).strip()
rows=json.loads((out/'final/continuation-runs.json').read_text())
engine=ET.parse(out/'engine-final.xml').getroot()
summary={'tested_commit':head,'base':base,'environment':'Linux x86_64, GCC13.3, SDL3 external prefix, fast dev -O0 -g1, -j16; native NVIDIA RTX3070Ti Laptop driver2435612928; Chromium SwiftShader compute','build_command':'GLOB2_GLSLANG=artifacts/gradient-tools/glslang-build/StandAlone/glslang GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py unit-tests engine-tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 -j16','results':{'native_unit':'130 cases, 691810 assertions passed with GLOB2_REQUIRE_VULKAN=1','engine':engine.attrib,'build_contracts':'564 tests passed, 4 skipped using pinned asset-encoder Python; system Python run is not valid tooling evidence','translations':'5 tests and strict audit passed; no structural errors','browser':'10 lifecycle tests and36 real SwiftShader compute comparisons passed','wasm_platform_wait':'actual em++ pthread Node smoke: owner=0 worker=1','prior_integration_continuations':'16/16 succeeded and all per-tick checksums matched','final_continuations':[{'label':r['label'],'exit':r['exit'],'checksums_match':r.get('checksums_match'),'gpu_fields':r.get('gpu_fields')} for r in rows],'canonical_match':'842 scalar checksums match prior canonical trace'},'limitations':['Vulkan Frozen8 and Jacobi unqualified: reproduced proof failures stop before publication; final Jacobi8 continuation failed','Resident GPU preparation kernels/stores absent and disabled','No release performance or Apple/non-Linux device/full threaded-browser qualification','Hardware syntax/mocked tests do not establish correctness'],'retained_failure':'artifacts/gradient-proof-vulkan-jacobi-110745987169842-0.bin'}
(out/'merge-verification.json').write_text(json.dumps(summary,indent=2)+'\n')
files=[]
for folder,label in [(out,'integration'),(root/'artifacts/gradient-review','review')]:
    for path in sorted(folder.glob('*')):
        if path.is_file() and path.suffix in ('.log','.json','.xml','.cpp','.py') and 'pr-body' not in path.name:
            files.append((path,Path(label)/path.name))
for path in (out/'final').rglob('*'):
    if path.is_file() and path.name in ('result.json','world.checksums','final.game.gz'):
        files.append((path,Path('integration/final')/path.relative_to(out/'final')))
for folder in (out/'golden-scalar', root/'artifacts/gradient-review/golden-scalar-buckets-0'):
    for filename in ('checksums.txt','verdict.json','result.json','match.replay'):
        path=folder/filename
        if path.exists():files.append((path,Path('canonical')/folder.name/filename))
for name in ('gradient-proof-vulkan-frozen8-108285169698217-0.bin','gradient-proof-vulkan-jacobi-108306663445367-0.bin','gradient-proof-vulkan-jacobi-110745987169842-0.bin'):
    for suffix in ('','.txt'):
        path=root/'artifacts'/(name+suffix)
        if path.exists():files.append((path,Path('proofs')/path.name))
receipt={str(destination):hashlib.sha256(path.read_bytes()).hexdigest() for path,destination in files}
(out/'evidence-hashes.json').write_text(json.dumps(receipt,indent=2)+'\n');files.append((out/'evidence-hashes.json',Path('evidence-hashes.json')))
with tempfile.TemporaryDirectory() as temporary:
    env={**os.environ,'GIT_INDEX_FILE':str(Path(temporary)/'index')}
    subprocess.run(['git','read-tree','--empty'],env=env,check=True)
    for source,destination in files:
        blob=subprocess.check_output(['git','hash-object','-w',str(source)],text=True).strip()
        subprocess.run(['git','update-index','--add','--cacheinfo',f'100644,{blob},{destination}'],env=env,check=True)
    tree=subprocess.check_output(['git','write-tree'],env=env,text=True).strip()
    commit=subprocess.check_output(['git','commit-tree',tree,'-m',f'Gradient merge evidence for {head}: qualified CPU contracts and unqualified Vulkan failures'],text=True).strip()
    subprocess.run(['git','push','origin',f'{commit}:refs/heads/codex/runtime-gradient-merge-evidence'],check=True)
print(commit,len(files),'evidence files')
(out/'evidence-commit.txt').write_text(commit+'\n')
