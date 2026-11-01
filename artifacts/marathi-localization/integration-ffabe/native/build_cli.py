from pathlib import Path
path=Path(__file__).with_name('focused_pipeline.py');exec(path.read_text().split('p=argparse.ArgumentParser();')[0])
assert git('rev-parse','HEAD')=='cdd634cebcde401f1266b6739f8ea21094eb4e56' and not git('status','--porcelain')
run('explicit-native-cli-build',['python3','tools/dev_build.py','build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2','-j2'],1800)
assert (BUILD/'src/glob2').is_file()
write('native-cli-producer.json',{'actual_official_build_revision':git('rev-parse','HEAD'),'binary':str(BUILD/'src/glob2'),'binary_sha256':sha(BUILD/'src/glob2'),'source_carry':'Current cdd is fixture-only follow-up to clean e481; official builder reuses exact unchanged production objects and compiles the actual entry point.','command_record':'explicit-native-cli-build-command.json'})
print('Actual native production CLI built.',flush=True)
