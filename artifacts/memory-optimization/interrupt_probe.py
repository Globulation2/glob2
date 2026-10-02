import pathlib,importlib.util,signal,sys
root=pathlib.Path(__file__).resolve().parents[2];spec=importlib.util.spec_from_file_location('b',root/'tools/memory_benchmark.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);signal.signal(signal.SIGTERM,m.stop_on_signal)
dirs={v:pathlib.Path(sys.argv[1])/v for v in ('baseline','candidate')}
for d in dirs.values():d.mkdir(exist_ok=True)
commands={v:[sys.executable,'-c','import os,time;print(os.getpid(),flush=True);time.sleep(60)'] for v in dirs}
m.run_pair(commands,dirs,tuple(dirs),{},.025)
