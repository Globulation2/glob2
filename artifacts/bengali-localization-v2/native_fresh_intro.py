from pathlib import Path
import os,subprocess,json,hashlib
root=Path.cwd();out=root/'artifacts/bengali-localization-v2';profile=out/'profiles/fresh-phone';profile.mkdir(parents=True,exist_ok=True)
prefs=profile/'preferences.txt';prefs.write_text('version=2\nlanguage=bn\ntextSizePercent=100\nmute=1\naudioPreferenceChosen=1\nthumbSideChosen=1\nintroductionOffered=0\n')
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_MOBILE_UI='touch-auto',GLOB2_USER_DATA_DIR=str(profile))
program=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/test/TranslationPreviewHarness';rev=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
for mode,screen in [('game','tutorial')]:
 args=['xvfb-run','-a',str(program),mode,str(root),str(out/'preview/bn'),str(out/'native/fresh-phone'),'412','839',screen]
 with (out/('native-fresh-phone-'+screen+'.log')).open('w') as f:r=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT)
 record=dict(revision=rev,command=args,environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','GLOB2_MOBILE_UI','GLOB2_USER_DATA_DIR']},profile_sha256=hashlib.sha256(prefs.read_bytes()).hexdigest(),text_size_percent=100,exit_code=r.returncode,scope='Fresh owned profile with introduction not previously offered; actual compact desktop engine, not native mobile coverage.')
 (out/('native-fresh-phone-'+screen+'.json')).write_text(json.dumps(record,indent=2)+'\n');print(screen,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
