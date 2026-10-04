import ctypes,ctypes.util,json,subprocess
from pathlib import Path
lib=ctypes.CDLL(ctypes.util.find_library('opusfile'))
for name,args,result in [('op_open_file',[ctypes.c_char_p,ctypes.POINTER(ctypes.c_int)],ctypes.c_void_p),('op_pcm_total',[ctypes.c_void_p,ctypes.c_int],ctypes.c_longlong),('op_link_count',[ctypes.c_void_p],ctypes.c_int),('op_seekable',[ctypes.c_void_p],ctypes.c_int),('op_channel_count',[ctypes.c_void_p,ctypes.c_int],ctypes.c_int),('op_pcm_seek',[ctypes.c_void_p,ctypes.c_longlong],ctypes.c_int),('op_raw_seek',[ctypes.c_void_p,ctypes.c_longlong],ctypes.c_int),('op_read_stereo',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_int16),ctypes.c_int],ctypes.c_int),('op_free',[ctypes.c_void_p],None)]:
 fn=getattr(lib,name);fn.argtypes=args;fn.restype=result
records=[]
for path in sorted(Path('data/zik').rglob('*.opus')):
 error=ctypes.c_int();track=lib.op_open_file(str(path).encode(),ctypes.byref(error));assert track,(path,error.value)
 try:
  assert lib.op_link_count(track)==1 and lib.op_seekable(track) and lib.op_channel_count(track,-1)==2
  total=lib.op_pcm_total(track,-1);assert total>0
  buf=(ctypes.c_int16*4096)();actual=0
  while True:
   n=lib.op_read_stereo(track,buf,4096);assert n>=0,(path,n)
   if not n:break
   actual+=n
  assert actual==total,(path,actual,total)
  for target in [0,total//2,total-13,0]:
   assert not lib.op_raw_seek(track,0) and not lib.op_pcm_seek(track,target),(path,target)
   n=lib.op_read_stereo(track,buf,4096);assert n==min(2048,total-target) or 0<n<=min(2048,total-target)
  old=path.with_suffix('.ogg')
  before=int(subprocess.check_output(['git','cat-file','-s',f'origin/master:{old}']))
  records.append(dict(path=str(path),frames=total,seconds=total/48000,before_bytes=before,after_bytes=path.stat().st_size))
 finally:lib.op_free(track)
for directory in Path('data/zik').iterdir():
 if directory.is_dir():
  trio=[r['frames'] for r in records if Path(r['path']).parent==directory];assert len(trio)==3 and len(set(trio))==1,(directory,trio)
assert len(records)==32,len(records)
assert not list(Path('data/zik').rglob('*.ogg'))
summary=dict(tracks=records,before_bytes=sum(r['before_bytes'] for r in records),after_bytes=sum(r['after_bytes'] for r in records))
Path('artifacts/opus/all-assets-validation.json').write_text(json.dumps(summary,indent=2));print(summary['before_bytes'],summary['after_bytes'],len(records))
