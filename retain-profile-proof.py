import ast, hashlib, importlib.util, json, os, shlex, subprocess
from pathlib import Path
root=Path.cwd(); output=root/'artifacts/coverage-disk/profile-proof';output.mkdir(exist_ok=True)
module_ast=ast.parse((root/'test/test_run_coverage.py').read_text())
source_text=next(ast.literal_eval(node.args[0]) for node in ast.walk(module_ast)
    if isinstance(node,ast.Call) and isinstance(node.func,ast.Attribute) and node.func.attr=='write_text'
    and isinstance(node.func.value,ast.Name) and node.func.value.id=='source')
source=output/'fixture.cpp';source.write_text(source_text)
spec=importlib.util.spec_from_file_location('coverage_runner',root/'test/run_coverage.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
commands=[];rows=[];exports=[]
def execute(command,**kwargs):
    commands.append(list(map(str,command)))
    return subprocess.run(command,check=True,capture_output=True,**kwargs)
for debug in ('full','line-tables-only'):
    binary=output/debug
    execute(['clang++-18',*shlex.split(module.instrumentation_flags(0,debug)),str(source),'-o',str(binary)])
    for pool in (0,4):
        directory=output/f'{debug}-{pool}';directory.mkdir(exist_ok=True)
        env=dict(os.environ,LLVM_PROFILE_FILE=str(directory/module.profile_pattern(pool)))
        statuses=[]
        for start in range(0,16,4):
            children=[subprocess.Popen([str(binary),str(n)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE) for n in range(start,start+4)]
            for n,child in zip(range(start,start+4),children):
                stdout,stderr=child.communicate(timeout=30);assert not stdout and not stderr
                assert child.returncode==(3 if n==7 else 0);statuses.append(child.returncode)
        profiles=sorted(directory.glob('*.profraw'));assert 0<len(profiles)<=(pool or 16)
        profdata=directory/'coverage.profdata'
        execute(['llvm-profdata-18','merge','-sparse',*map(str,profiles),'-o',str(profdata)])
        result=execute(['llvm-cov-18','export',str(binary),f'-instr-profile={profdata}','-skip-expansions',str(source)],text=True)
        export=json.loads(result.stdout);exports.append(export)
        (directory/'coverage.json').write_text(result.stdout)
        assert next(f for f in export['data'][0]['functions'] if f['name']=='main')['count']==16
        assert module.compact_profiles(profiles,False)=={'raw_profiles_retained':True}
        assert all(p.exists() for p in profiles)
        rows.append(dict(debugInfo=debug,pool=pool,processes=len(statuses),failedProcesses=statuses.count(3),profiles=len(profiles),binaryBytes=binary.stat().st_size,exportSha256=hashlib.sha256(result.stdout.encode()).hexdigest()))
assert all(x==exports[0] for x in exports[1:])
(output/'results.json').write_text(json.dumps({'rows':rows,'completeExportsEqual':True,'commands':commands,'clang':execute(['clang++-18','--version'],text=True).stdout},indent=2)+'\n')
print(json.dumps(rows,indent=2))
