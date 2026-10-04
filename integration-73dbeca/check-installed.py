from pathlib import Path
import subprocess,os
root=Path.cwd();binary=root/'artifacts/opus/latest-install-bin/glob2';prefix=root/'build/sdl3-opus/prefix';hidden=prefix.with_name('prefix-unavailable-verification')
assets=root/'artifacts/opus/latest-install-root/glob2/data/zik'
assert len(list(assets.rglob('*.opus')))==32
assert len(list(assets.rglob('LICENSE.txt')))==9
assert not list(assets.rglob('*.ogg'))
out=subprocess.check_output(['readelf','-d',binary],text=True)
assert str(prefix) not in out
prefix.rename(hidden)
try:
 env=os.environ.copy();env.pop('LD_LIBRARY_PATH',None)
 p=subprocess.run([str(binary),'--version'],env=env,text=True,capture_output=True)
 assert p.returncode==0,(p.stdout,p.stderr)
 linked=subprocess.check_output(['ldd',binary],env=env,text=True)
 assert 'vorbis' not in linked and 'not found' not in linked
 text='Inventory: 32 Opus, 9 LICENSE, no Vorbis music.\nInstalled executable starts without development SDL prefix and LD_LIBRARY_PATH.\n'+p.stdout+p.stderr+out+linked
 (root/'artifacts/opus/rebase-latest-install-check.log').write_text(text)
 print(text[:500])
finally:hidden.rename(prefix)
