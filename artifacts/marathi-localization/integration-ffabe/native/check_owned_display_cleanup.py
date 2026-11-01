from pathlib import Path
import subprocess,os,json,signal,time
root=Path.cwd();prefix=str(root/'artifacts/marathi-localization/integration-ffabe/native');owned=[];foreign_count=0
for line in subprocess.check_output(['ps','-eo','pid,ppid,comm'],text=True).splitlines()[1:]:
 pid,ppid,comm=line.split(None,2)
 if comm not in ('Xvfb','openbox'):continue
 proc=Path('/proc')/pid;reasons=[]
 try:
  for fd in (proc/'fd').iterdir():
   try:
    target=os.readlink(fd)
    if target.startswith(prefix):reasons.append('open evidence fd: '+target)
   except OSError:pass
  raw=(proc/'environ').read_bytes().split(b'\0')
  for item in raw:
   name,sep,value=item.partition(b'=')
   if name in (b'GLOB2_USER_DATA_DIR',b'GLOB2_TEST_ARTIFACTS',b'GLOB2_TEST_ARTIFACTS_ROOT') and value.decode(errors='replace').startswith(prefix):reasons.append('task-owned isolated profile/artifact prefix')
 except (OSError,PermissionError):pass
 if reasons:owned.append({'pid':int(pid),'ppid':int(ppid),'command':comm,'ownership_evidence':reasons})
 else:foreign_count+=1
for item in owned:
 try:os.kill(item['pid'],signal.SIGTERM)
 except ProcessLookupError:pass
for _ in range(40):
 alive=[x['pid'] for x in owned if Path('/proc',str(x['pid'])).exists() and Path('/proc',str(x['pid']),'stat').read_text().split()[2]!='Z']
 if not alive:break
 time.sleep(.25)
assert not alive,alive
record={'scope':'Only Xvfb/Openbox with exact current Marathi integration evidence fds or isolated profile/artifact prefix; no other chat/session processes touched. No environment dump recorded.','owned_remaining_before_cleanup':owned,'owned_remaining_after_cleanup':[],'other_display_processes_left_untouched':foreign_count,'disposition':'All current native capture/test runners completed and their finally blocks reaped owned displays; no forced termination required.' if not owned else 'Only positively identified unused task-owned display processes gracefully terminated.'};(root/'artifacts/marathi-localization/integration-ffabe/native/display-cleanup.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
