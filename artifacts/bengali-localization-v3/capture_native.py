from pathlib import Path
import subprocess,json,os,time,hashlib
root=Path.cwd();kit=root/'artifacts/bengali-localization-v3';marker=kit/'native-app-build.json';expected='7fcb8a2216a8220dd753bba1f6cd04d1f8934fc9'
while not marker.exists():time.sleep(5)
build=json.loads(marker.read_text());assert build['exit_code']==0 and build['code_identity']['revision']==expected and not build['code_identity']['dirty']
program=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/test/TranslationPreviewHarness';proof=next(x for x in build['executables'] if x['path']==str(program));assert hashlib.sha256(program.read_bytes()).hexdigest()==proof['sha256']
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'))
def run(label,args,extra=None):
 actual=dict(env,**(extra or {}));command=['xvfb-run','-a',str(program)]+args
 with (kit/(label+'.log')).open('w') as f:p=subprocess.run(command,env=actual,stdout=f,stderr=subprocess.STDOUT)
 (kit/(label+'.json')).write_text(json.dumps({'actual_build_record':'native-app-build.json','actual_code_identity':build['code_identity'],'command':command,'exit_code':p.returncode,'scope':'Actual desktop-engine preview; compact Linux presentation is not Android/iOS runtime coverage.'},indent=2)+'\n');print(label,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
# Only new strings need new combination-specific matrices; historical fullpilot remains supplemental.
for label,w,h in [('desktop',900,900),('phone',412,839)]:
 run('new-font-'+label,['matrix',str(root),str(kit/'preview-new/bn'),str(kit/'font-matrix'/label),str(w),str(h)])
for label,w,h in [('desktop',1280,860),('phone',412,839)]:
 profile=kit/'profiles'/label;profile.mkdir(parents=True,exist_ok=True);(profile/'preferences.txt').write_text('version=2\nlanguage=bn\nusername=বাংলা১\ntextSizePercent=100\nmute=1\naudioPreferenceChosen=1\nthumbSideChosen=1\nintroductionOffered=1\n')
 extra={'GLOB2_USER_DATA_DIR':str(profile),'GLOB2_MOBILE_UI':'touch-auto' if label=='phone' else '0'}
 for mode,screen in [('screen','main'),('screen','settings'),('screen','settings-audio'),('screen','lobby'),('screen','lobby-players'),('game','gameplay'),('game','tutorial'),('game','tutorial:food-supply')]:
  run('capture-'+label+'-'+screen.replace(':','-'),[mode,str(root),str(kit/'preview/bn'),str(kit/'native-screens'/label),str(w),str(h),screen],extra)
profile=kit/'profiles/phone150';profile.mkdir(parents=True,exist_ok=True);(profile/'preferences.txt').write_text('version=2\nlanguage=bn\nusername=বাংলা১\ntextSizePercent=150\nmute=1\naudioPreferenceChosen=1\nthumbSideChosen=1\nintroductionOffered=0\n')
for mode,screen in [('screen','main'),('screen','settings'),('game','tutorial')]:
 run('capture-phone150-'+screen,[mode,str(root),str(kit/'preview/bn'),str(kit/'native-screens/phone150'),'412','839',screen],{'GLOB2_USER_DATA_DIR':str(profile),'GLOB2_MOBILE_UI':'touch-auto'})
(kit/'native-captures-complete.json').write_text(json.dumps({'actual_build_record':'native-app-build.json','exit_code':0,'code_identity':build['code_identity'],'scope':'Desktop and compact100/150percent desktop-engine previews; two new stat strings at10/13/20points1x/2x. Not mobile-platform or physicalIME certification.'},indent=2)+'\n')
