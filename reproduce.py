from pathlib import Path
import argparse,json,os,subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--master-root',type=Path,required=True)
parser.add_argument('--pr-root',type=Path,required=True)
args=parser.parse_args()
evidence=Path(__file__).resolve().parent
for label in ['master-1','pr-1','pr-2','master-2','master-3','pr-3']:
    metadata=json.loads((evidence/(label+'-command.json')).read_text())
    master=label.startswith('master')
    root=(args.master_root if master else args.pr_root).resolve()
    binary=root/('artifacts/render-master-build/test/torus-render-benchmark' if master else
                 'artifacts/render-production/build/test/torus-render-benchmark')
    environment=metadata['environment']|{
       'GLOB2_USER_DATA_DIR':str(evidence/'profile'),
       'GLOB2_BENCH_GAME':str(evidence/'checkpoint-20000.game.gz'),
       'GLOB2_BENCH_CAPTURE':str(evidence/(label+'-reproduced.ppm'))}
    with (evidence/(label+'-reproduced.log')).open('w') as log:
        subprocess.run([str(binary),'-g','-F','-m','-s','1024x600'],cwd=root,
            env=os.environ|environment,stdout=log,stderr=subprocess.STDOUT,check=True)
