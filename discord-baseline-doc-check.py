from pathlib import Path
import subprocess,runpy,sys
path=Path('docs/multiplayer/client.md').resolve()
baseline=subprocess.check_output(['git','show','7d17f40d156fa847cc8e585907aee1d13ac6fdd0:docs/multiplayer/client.md'],text=True)
read=Path.read_text
def baseline_read(self,*args,**kwargs):
 return baseline if self.resolve()==path else read(self,*args,**kwargs)
Path.read_text=baseline_read
sys.argv=['tools/check_docs.py']
runpy.run_path('tools/check_docs.py',run_name='__main__')
