from pathlib import Path
import os,subprocess
p=Path(__file__).resolve().parent;package=p/'publish'
assert (package/'manifest.json').exists()
ref='refs/heads/codex/lazy-gradient-evidence'
def git(*args,data=None,env=None):return subprocess.check_output(['git',*args],input=data,env=env).decode().strip()
old=git('rev-parse',ref)
assert old=='edf80b22276a345167bbc837b0d38fef9b11d1db'
index=p/'evidence-index';assert not index.exists()
env=os.environ.copy();env['GIT_INDEX_FILE']=str(index)
git('read-tree',old,env=env)
for f in sorted(package.rglob('*')):
 if f.is_file():
  blob=git('hash-object','-w',str(f));git('update-index','--add','--cacheinfo',f'100644,{blob},field-initialization/{f.relative_to(package)}',env=env)
s=git('show',f'{old}:README.md')
s=s.replace('# Lazy building gradient validation evidence\n','# Lazy building gradient validation evidence\n\n**Single-pass field initialization:** [incremental optimization evidence](field-initialization/README.md) includes AC timings, initialization/fill profiles, and cell-by-cell comparison against the previous initializer.\n',1)
blob=git('hash-object','-w','--stdin',data=(s+'\n').encode());git('update-index','--add','--cacheinfo',f'100644,{blob},README.md',env=env)
tree=git('write-tree',env=env);commit=git('commit-tree',tree,'-p',old,data=b'Retain single-pass field initialization timings and equivalence checks\n');git('update-ref',ref,commit,old)
(p/'evidence-commit.txt').write_text(commit+'\n');print(commit)
