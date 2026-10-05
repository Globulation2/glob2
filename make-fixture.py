from pathlib import Path
import subprocess, zipfile
root=Path('artifacts/audio/music-fixture');root.mkdir(exist_ok=True)
for n,mood in enumerate(('calm','building','combat'),1):
 tags={'GLOB2_SCHEMA':'1','GLOB2_RELEASE':'11111111-1111-4111-8111-111111111111','GLOB2_ORIGIN':'','ALBUM':'Moss lantern','ARTIST':'Audio test fixture','DESCRIPTION':'Generated test tones','LICENSE':'CC0-1.0','COPYRIGHT':'Test fixture','SOURCE':'Generated sine tones','GLOB2_AI_GENERATED':'0','GLOB2_FRAMES':'480000','GLOB2_MOOD':mood}
 # Empty tag values are omitted by ffmpeg; use an explicit test origin.
 tags['GLOB2_ORIGIN']='https://example.invalid'
 command=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'sine=frequency={220*n}:duration=10:sample_rate=48000','-ac','2','-c:a','libopus','-b:a','96k']
 for key,value in tags.items():command+=['-metadata',f'{key}={value}']
 subprocess.run(command+[str(root/f'a{n}.opus')],check=True)
with zipfile.ZipFile(root/'music.zip','w') as z:
 for n in range(1,4):z.write(root/f'a{n}.opus',f'a{n}.opus')
