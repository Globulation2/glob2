from pathlib import Path
import re,shutil
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';source=base/'c-layout-src'
def strip(text):
 result=[];stack=[]
 for line in text.splitlines(keepends=True):
  s=line.strip()
  m=re.fullmatch(r'#ifndef GROWTH_OPT_([ABCD])',s)
  if m:stack.append(False);continue
  m=re.fullmatch(r'#if (!?)GROWTH_OPT_([ABCD])',s)
  if m:stack.append(not bool(m[1]));continue
  if s=='#else' and stack:stack[-1]=not stack[-1];continue
  if s=='#endif' and stack:stack.pop();continue
  if all(stack):result.append(line)
 assert not stack
 output=''.join(result);assert 'GROWTH_OPT_' not in output
 return output
names=['src/map/Map.h','src/map/MapResourceState.cpp','src/map/ResourceGrowth.cpp','src/engine/sim/snapshot/WorldSnapshot.h','src/engine/sim/snapshot/WorldCapture.cpp']
for name in names:
 text=(source/name).read_text()
 if name.endswith('.cpp'):text=strip(text)
 (root/name).write_text(text)
print('Installed all-four production sources without experimental switches')
