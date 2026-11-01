"""Resume after recorded recipe-only SCons -B rejection; no object recompilation."""
from pathlib import Path
import json,gzip,shlex
path=Path(__file__).with_name('focused_pipeline.py')
exec(path.read_text(encoding='utf-8').split('p=argparse.ArgumentParser();')[0])
info=json.loads((BASE/'frozen-inputs.json').read_text(encoding='utf-8'));check_inputs(info)
assert json.loads((BASE/'focused-object-build-command.json').read_text(encoding='utf-8'))['exit_code']==0
sources=__import__('sys');sources.path.insert(0,str(ROOT/'test'));import tests as registry
sources=registry.SUPPORT+registry.ENGINE_SUPPORT+ENGINE_CASES
objects=[BUILD/'test'/('engine-'+s.lstrip('#').replace('/','_').rsplit('.',1)[0]+'.o') for s in sources]
lines=gzip.open(BASE/'official-build.log.gz','rt',encoding='utf-8').read().splitlines()
link=shlex.split(next(l for l in lines if l.startswith('g++ -o ') and '/TranslationPreviewHarness ' in l));idx=next(i for i,s in enumerate(link) if s.endswith('/program-tools_TranslationPreviewHarness.o'))
link[link.index('-o')+1]=str(BASE/'focused-engine-tests');link[idx:idx+1]=map(str,objects)
run('focused-link',link,600)
slot=BASE/'focused-build/test';slot.mkdir(parents=True,exist_ok=True);(slot/'glob2-engine-tests').symlink_to(BASE/'focused-engine-tests')
run('focused-inventory',[str(BASE/'focused-engine-tests'),'-ltc'],300,video='dummy')
run('relay-build',['python3','tools/dev_build.py','role=relay','relay-tests','-j4'],3600)
relay=ROOT/'build/linux/relay/debug/dev-dev_fast-true-linker-auto/src/glob2-relay-tests';assert relay.is_file()
info.update(focused_binary=str(BASE/'focused-engine-tests'),focused_sha256=sha(BASE/'focused-engine-tests'),official_harness_sha256=sha(BUILD/'test/TranslationPreviewHarness'),native_cli_sha256=sha(BUILD/'src/glob2'),registered_sources=sources,relay_binary=str(relay),relay_sha256=sha(relay),environment_prefixes={k:ENV[k] for k in ('GLOB2_SDL3_PREFIX','GLOB2_RECORDING_PREFIX')},recipe_source='official-build.log.gz (actual executed successful build, no forced dry run)',scaffolding_failure_preserved='official-link-recipe-command.json + official-link-recipe.log.gz')
check_inputs(info);write('build-ready.json',info);print('Focused registry and relay binaries ready; golden update is owned by early_golden.py.',flush=True)
