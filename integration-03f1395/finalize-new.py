import sys,json,subprocess,hashlib
from pathlib import Path
sys.path.insert(0,str(Path('tools/music').resolve()))
from glob2music.audio import Trio,read_audio,read_trio,trio_paths
from glob2music.manifest import encode_within_ceiling,encoding_metadata,load_manifest,set_spec
from glob2music.qa import run_checks
from glob2music import cli,preview
import soundfile as sf
names = ['moss-lanterns','thistle-waltz','bramble-jig','fennel-mist','glass-garden','woodland','apple-cider','curious-critters','orchestral-dawn']
if len(sys.argv)>1: names=names[names.index(sys.argv[1]):]
for name in names:
 out=Path('tools/music/out')/name
 trio=Trio(**{m:read_audio(out/'pcm'/f'a{i}.wav')[0] for i,m in enumerate(['calm','building','combat'],1)})
 manifest=load_manifest(name)
 spec=set_spec(manifest)
 finished=encode_within_ceiling(trio,out,spec,seam_ms=manifest.master.get('opus_seam_ms'))
 report=run_checks(out,spec=spec,waivers=manifest.waivers)
 (out/'qa.json').write_text(report.to_json())
 meta=json.loads((out/'build.json').read_text())
 meta.update(finished.meta)
 meta['encoding']=encoding_metadata(finished,out,manifest)
 (out/'build.json').write_text(json.dumps(meta,indent=2))
 print('QA',name,report.failed,finished.meta,flush=True)
 if report.failed:
  print(report.format_table(),flush=True)
  raise RuntimeError(name+' QA failed')
 if cli.main(['install',name]):raise RuntimeError(name+' install failed')
 review=Path('artifacts/opus/new-audio-samples')/name;review.mkdir(parents=True,exist_ok=True)
 decoded=read_trio(out)
 sf.write(review/'mood-transitions.flac',preview.render(decoded,segment_s=8),48000)
 for mood,path in trio_paths(out).items():
  y,_=read_audio(path)
  import numpy as np
  sf.write(review/f'{mood}-loop-boundary.flac',np.concatenate([y[-48000*3:],y[:48000*3]]),48000)
  source=review/f'{mood}-before.ogg'
  old=f'data/zik/{name}/{path.stem}.ogg'
  source.write_bytes(subprocess.check_output(['git','show',f'origin/master:{old}']))
  subprocess.run(['ffmpeg','-v','error','-y','-i',str(source),'-t','12','-ar','48000',str(review/f'{mood}-before.flac')],check=True)
  source.unlink()
  sf.write(review/f'{mood}-after.flac',y[:48000*12],48000)
