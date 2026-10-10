import ast,json,re,subprocess
from pathlib import Path
base='origin/master'
paths=subprocess.check_output(['git','diff','--name-only',base]).decode().splitlines()
pattern=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[^\s]')
def tokens(s):
 result=[]
 for m in pattern.finditer(s):
  t=m.group()
  if t.startswith(('//','/*')): continue
  if t.startswith('"'): t=re.sub(r'docs/[A-Za-z0-9_./-]+\.(?:md|txt)(?:#[A-Za-z0-9_%-]+)?','DOC_PATH',t)
  result.append(t)
 return result
class StripDocstrings(ast.NodeTransformer):
 def visit(self,node):
  node=super().visit(node)
  if isinstance(node,(ast.Module,ast.ClassDef,ast.FunctionDef,ast.AsyncFunctionDef)) and node.body and isinstance(node.body[0],ast.Expr) and isinstance(node.body[0].value,ast.Constant) and isinstance(node.body[0].value.value,str): node.body=node.body[1:]
  return node
def tree(s): return ast.dump(StripDocstrings().visit(ast.parse(s)),include_attributes=False)
rows=[]
for name in paths:
 p=Path(name)
 if not p.exists(): continue
 if name.startswith('src/') and p.suffix in ('.h','.cpp'):
  old=subprocess.check_output(['git','show',base+':'+name]).decode()
  rows.append({'file':name,'only_comments_or_documentation_strings':tokens(old)==tokens(p.read_text())})
 elif name.startswith(('tools/cortex-ml/','tools/cortex-ml-infer/')) and p.suffix=='.py':
  old=subprocess.check_output(['git','show',base+':'+name]).decode()
  rows.append({'file':name,'only_comments_or_docstrings':tree(old)==tree(p.read_text())})
Path('artifacts/documentation-review/source-proof.json').write_text(json.dumps(rows,indent=2)+'\n')
print(f'Reviewed {len(rows)} native/Cortex Python files; {sum(not list(r.values())[1] for r in rows)} behavioral differences')
assert all(list(r.values())[1] for r in rows)
