def processes():
 lines=subprocess.check_output(['ps','-axo','pid=,pcpu=,comm=,args='],text=True).splitlines();found=[]
 for line in lines:
  parts=line.strip().split(None,3)
  if len(parts)<3:continue
  pid,cpu,command=parts[:3];exe=pathlib.Path(command).name.lower()
  assert 'glob2' not in exe and exe not in {'clang','clang++','g++','gcc','cc1','cc1plus','ld','ld64','scons'},'Competing game/build: '+line
  if exe.startswith('python') and len(parts)>3:
   assert not any(('qualify' in x and x.endswith('.py')) or pathlib.Path(x).name in {'scons','scons.py'} for x in parts[3].split()),'Competing qualification: '+line
  found.append(dict(pid=int(pid),cpu_percent=cpu,command=command))
 return found
def context():
 return dict(utc=now(),loadavg=os.getloadavg(),processes=processes(),thermal=subprocess.run(['pmset','-g','therm'],text=True,capture_output=True).stdout,power=subprocess.run(['pmset','-g','batt'],text=True,capture_output=True).stdout)
def dependencies(binary):
 direct=subprocess.check_output(['otool','-L',str(binary)],text=True).splitlines()[1:];queue=[(line.strip().split(' (')[0],pathlib.Path(binary)) for line in direct];files={};system=[]
 while queue:
  name,loader=queue.pop()
  if name.startswith('@loader_path/'):
   name=str(loader.parent/name.removeprefix('@loader_path/'))
  elif name.startswith('@rpath/'):
   commands=subprocess.check_output(['otool','-l',str(loader)],text=True).splitlines();rpaths=[]
   for i,line in enumerate(commands):
    if line.strip()=='cmd LC_RPATH':
     rpath=commands[i+2].strip().split(' (offset')[0].removeprefix('path ').replace('@loader_path',str(loader.parent)).replace('@executable_path',str(pathlib.Path(binary).parent));rpaths.append(pathlib.Path(rpath)/name.removeprefix('@rpath/'))
   matches=[p for p in rpaths if p.is_file()];assert matches,'Unresolved rpath dependency '+name+' from '+str(loader);name=str(matches[0])
  if name in files or name in system:continue
  path=pathlib.Path(name)
  if name.startswith('/usr/lib/') or name.startswith('/System/'):
   system.append(name);continue
  assert path.is_file(),'Unresolved non-system dependency: '+name
  files[name]=dict(resolved_path=str(path.resolve()),sha256=sha(path))
  queue += [(line.strip().split(' (')[0],path) for line in subprocess.check_output(['otool','-L',name],text=True).splitlines()[1:] if line.strip().split(' (')[0]!=name]
 return dict(direct_commands=direct,non_system_dylibs=files,dyld_shared_cache_paths=sorted(system))
def tree(root):return {str(p.relative_to(root)):sha(p) for p in root.rglob('*') if p.is_file()}
