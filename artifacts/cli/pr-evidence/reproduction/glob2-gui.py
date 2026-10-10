import subprocess,os,time,json
from pathlib import Path
root=Path.cwd();binary=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2';out=root/'artifacts/cli/gui';out.mkdir(exist_ok=True)
cases=[('default',[]),('play-record',['play','--window-size','640x480','--renderer','software','--no-fullscreen','--resizable','--no-custom-cursor','--mute','--graphics-detail','reduced','--record',str(out/'menus.mp4'),'--record-encoder','software','--record-fps','15','--record-crf','25','--record-chapter-ticks','64']),('replay',['replay',str(root/'artifacts/cli/parity-final/game-new/game.replay'),'--window-size','640x480','--renderer','software','--no-fullscreen','--mute'])]
records=[]
for name,args in cases:
 env=dict(os.environ,GLOB2_USER_DATA_DIR=str(out/(name+'-profile')),SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',LIBGL_ALWAYS_SOFTWARE='1')
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.Popen([str(binary),*args],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+30;window=None
   while time.monotonic()<deadline:
    if p.poll() is not None:raise RuntimeError(name+' exited '+str(p.returncode))
    found=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(p.pid)],capture_output=True,text=True)
    if found.returncode==0:window=found.stdout.splitlines()[0];break
    time.sleep(.2)
   assert window,name+' did not display a window'
   time.sleep(3)
   geometry=subprocess.check_output(['xdotool','getwindowgeometry','--shell',window],text=True)
   subprocess.run(['xdotool','windowactivate','--sync',window],check=True)
   subprocess.run(['xdotool','key','--window',window,'alt+F4'],check=True)
   code=p.wait(timeout=30)
   assert code==0,(name,code)
   records.append({'name':name,'command':[str(binary),*args],'exit_code':code,'geometry':geometry})
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=10)
manifest=json.loads((out/'menus.mp4.json').read_text());assert manifest['complete'];assert manifest['frames']>0 if 'frames' in manifest else True
probe=subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=width,height,codec_name','-of','json',str(out/'menus.mp4')],text=True)
(out/'ffprobe.json').write_text(probe);(out/'launches.json').write_text(json.dumps(records,indent=2));print('Default GUI, explicit play/recording and replay launch completed; video finalized.')
