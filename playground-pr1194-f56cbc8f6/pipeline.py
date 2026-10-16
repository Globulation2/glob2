from pathlib import Path
import datetime,json,os,subprocess,time,traceback
O=Path(__file__).resolve().parent
def save(name,value):
    (O/name).write_text(json.dumps(value,indent=2)+'\n')
def main():
    save('pipeline-launch.json',{'pid':os.getpid(),'starttime':Path('/proc/self/stat').read_text().split(') ',1)[1].split()[19],'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
    deadline=time.monotonic()+7200
    while not (O/'build-complete.json').exists():
        assert time.monotonic()<deadline,'Build did not finish in two hours'
        time.sleep(3)
    for name,python in [('run.py','python3'),('analyze.py',str(O.parent/'october-regression/coordination-redesign/full-design-comparison/playground-stop/plot-venv/bin/python')),('compare-previous.py',str(O.parent/'october-regression/coordination-redesign/full-design-comparison/playground-stop/plot-venv/bin/python'))]:
        with (O/(name+'.console.log')).open('w') as log:
            result=subprocess.run([python,str(O/name)],stdout=log,stderr=subprocess.STDOUT)
        assert result.returncode==0,(name,result.returncode)
    save('pipeline-complete.json',{'state':'complete','utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
if __name__=='__main__':
    try:main()
    except BaseException:
        save('pipeline-failure.json',{'traceback':traceback.format_exc()})
        raise
