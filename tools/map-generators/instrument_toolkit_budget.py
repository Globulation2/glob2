#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add native-loop work checkpoints and checked vector indexing using libclang 18.

Development tool; shipping builds use the committed C++ source. Re-running is idempotent.
"""
import os,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scons'))
from sources import INCLUDE_DIRECTORIES
from clang import cindex as c
c.Config.set_library_file(os.getenv('LIBCLANG_PATH','/usr/lib/x86_64-linux-gnu/libclang-18.so.1'))
paths=sorted(p for p in (ROOT/'src/map/generator/shared').rglob('*.cpp') if not p.name.endswith('Test.cpp'))
paths += [ROOT/'src/map/generator/generators'/f'{name}Generator.cpp' for name in ['Forts','EvenGround','Swamp']]
flags=['-I'+str(ROOT/'third_party/quickjs-ng'),'-std=c++20','-I'+str(ROOT/'build/linux/client/release/include')]+['-I'+str(ROOT/p)for p in INCLUDE_DIRECTORIES]
if os.getenv('GLOB2_SDL3_PREFIX'):flags+=['-I'+os.environ['GLOB2_SDL3_PREFIX']+'/include']
changes={};texts={};loops=set()
def relevant(path):
 return '/map/generator/shared/'in path and not path.endswith(('Test.h','Test.cpp')) or path in [str(p)for p in paths[-3:]]
def walk(v):
 file=str(v.location.file)if v.location.file else ''
 if file and not relevant(file):return
 if file:
  text=texts.setdefault(file,Path(file).read_bytes().decode("latin1"));edits=changes.setdefault(file,{})
  if v.kind==c.CursorKind.CALL_EXPR and v.spelling=='operator[]':
   args=list(v.get_arguments())
   if len(args)==2 and args[0].type.get_canonical().spelling.removeprefix('const ').startswith('std::vector<'):
    start=args[0].extent.end.offset;end=v.extent.end.offset-1
    bracket=text.find('[',start,args[1].extent.start.offset+1)
    if bracket>=0 and end>bracket and text[end]==']':
     edits[bracket]=('.at(',1);edits[end]=(')',1)
  if v.kind in (c.CursorKind.FOR_STMT,c.CursorKind.CXX_FOR_RANGE_STMT,c.CursorKind.WHILE_STMT,c.CursorKind.DO_STMT):
   kids=list(v.get_children())
   if kids:
    body=kids[0]if v.kind==c.CursorKind.DO_STMT else kids[-1]
    start=body.extent.start.offset;end=body.extent.end.offset
    key=(file,start,end)
    if key not in loops and 'generationCheckpoint('not in text[start:start+100]:
     loops.add(key)
     if body.kind==c.CursorKind.COMPOUND_STMT:
      edits.setdefault(start+1,(' ::MapGeneration::generationCheckpoint(); ',0))
     else:
      if end<len(text)and text[end]==';':end+=1
      prefix,consume=edits.get(start,('',0));edits[start]=(' { ::MapGeneration::generationCheckpoint(); '+prefix,consume)
      suffix,consume=edits.get(end,('',0));edits[end]=(suffix+' } ',consume)
 for child in v.get_children():walk(child)
for path in paths:
 tu=c.Index.create().parse(str(path),args=flags)
 errors=[str(d)for d in tu.diagnostics if d.severity>=c.Diagnostic.Error]
 if errors:sys.exit('\n'.join(errors))
 walk(tu.cursor)
 print(path.name,flush=True)
for file,edits in changes.items():
 if not edits:continue
 text=texts[file]
 for index,(replacement,count)in sorted(edits.items(),reverse=True):text=text[:index]+replacement+text[index+count:]
 if 'GenerationWork.h'not in text:
  if '#pragma once'in text:text=text.replace('#pragma once','#pragma once\n#include "GenerationWork.h"',1)
  else:text='#include "GenerationWork.h"\n'+text
 Path(file).write_bytes(text.encode("latin1"))
print('Instrumented',len(changes),'files')
