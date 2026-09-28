from pathlib import Path
import os, subprocess, sys, tempfile, shutil
root=Path(__file__).resolve().parent
binary=Path(sys.argv[1]).resolve()
label=sys.argv[2]
mode=sys.argv[3]
with tempfile.TemporaryDirectory(prefix='glob2-landscape-') as temporary:
    home=Path(temporary)/'home'; home.mkdir()
    work=Path(temporary)/'work'; work.mkdir()
    (work/'maps').mkdir()
    shutil.copyfile(root.parents[1]/'maps/FourSquares1.map', work/'maps/FourSquares1.map')
    (work/'artifacts').symlink_to(root, target_is_directory=True)
    env=dict(os.environ, HOME=str(home), USERPROFILE=str(home), SDL_AUDIODRIVER='dummy')
    args=[str(binary)]
    if mode in ('preview-queue','preview-restart'):
        args.append(mode)
    elif mode=='map-headless':
        args.append('glob2-map-preview-tests')
    elif mode=='map-visual':
        args += ['glob2-map-preview-tests','--visual','artifacts/'+label]
    else:
        args += ['artifacts/'+label,mode]
    with (root/(label+'.log')).open('w') as output:
        print('Running',args,flush=True)
        result=subprocess.run(args,cwd=work,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=180)
    print('Exit',result.returncode,flush=True)
    raise SystemExit(result.returncode)
