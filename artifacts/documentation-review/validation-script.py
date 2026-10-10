import json, os, platform, subprocess, time
from pathlib import Path
root=Path.cwd(); evidence=root/'artifacts/documentation-review'
node='/Users/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node'
env=os.environ.copy(); env['PYTHONPATH']=str(root/'artifacts/docs-runtime')
checks=[
('source-proof', ['python3','artifacts/documentation-review/source-proof.py'], root),
('checker-tests', ['python3','-m','unittest','discover','-s','test','-p','test_check_docs.py','-v'], root),
('local-links', ['python3','tools/check_docs.py'], root),
('source-art-catalog', ['python3','tools/artwork/catalog_originals.py'], root),
('source-art-catalog-unchanged', ['git','diff','--exit-code','--','datasrc/gfx/CATALOG.md','datasrc/gfx/provenance/catalog.json'], root),
('provenance', ['python3','tools/artwork/runtime_provenance.py','--check'], root),
('sim-version', ['python3','test/check_sim_revision.py','--base','origin/master'], root),
('entity-random', ['python3','test/check_entity_random.py'], root),
('ci-policy-tests', ['python3','-m','unittest','discover','-s','test/build_system','-p','test_ci_policy.py','-v'], root),
('skin-materials-tests', ['python3','-m','unittest','discover','-s','test/build_system','-p','test_skin_materials.py','-v'], root),
('scene-boundary-tests', ['python3','-m','unittest','discover','-s','test/build_system','-p','test_scene_boundary.py','-v'], root),
('generator-studio-tests', [node,'node_modules/vitest/vitest.mjs','run','apps/api/test/generatorStudioProvider.test.ts'], root/'platform'),
('platform-format', [node,'node_modules/prettier/bin/prettier.cjs','--check','README.md','apps/web/art/README.md','apps/api/src/generator-studio/domain.ts','apps/web/src/pages/Generators.tsx','packages/core/src/queueConfig.ts','packages/engine/src/engineCli.ts'], root/'platform'),
('whitespace', ['git','diff','--check','origin/master'], root),
('privacy-policy', ['git','diff','--exit-code','origin/master','--','docs/mobile/privacy-policy.md','docs/mobile/amazon-privacy-policy.md'], root),
('build-system-tests', ['python3','-m','unittest','discover','-s','test/build_system','-v'], root),
('external-links', ['python3','tools/check_docs.py','--external'], root),
('partner-portal', ['curl','--silent','--show-error','--location','--output','/dev/null','--max-time','20','--write-out','%{http_code}\n','https://partner.microsoft.com/en-US/dashboard/products/9PH4FCRMX19F/setup'], root)
]
def revision(ref): return subprocess.check_output(['git','rev-parse',ref]).decode().strip()
results=dict(revision=revision('HEAD'), base=revision('origin/master'), os=platform.platform(), architecture=platform.machine(), python=platform.python_version(), node=subprocess.check_output([node,'--version']).decode().strip(), checks=[])
for name,cmd,cwd in checks:
    start=time.monotonic()
    with (evidence/(name+'.log')).open('w') as log:
        proc=subprocess.run(cmd,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
    results['checks'].append(dict(name=name,command=cmd,working_directory=str(cwd.relative_to(root)) or '.',exit_code=proc.returncode,seconds=round(time.monotonic()-start,3),log=name+'.log'))
    (evidence/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(f'{name}: {proc.returncode}',flush=True)
