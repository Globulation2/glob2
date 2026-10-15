"""Cleanup only descendants of a verified owned child, across process sessions."""
from pathlib import Path
import os,signal,json

def process_identity(pid):
 try:
  fields=(Path('/proc')/str(pid)/'stat').read_text().split(') ',1)[1].split();return int(fields[1]),fields[19]
 except (OSError,IndexError,ValueError):return None

def kill_owned_tree(pid, evidence_path):
 # Native cases start their own sessions. Freeze the owned tree before killing
 # it so no runner can dispatch another case during timeout cleanup.
 root=process_identity(pid)
 if root is None:return
 tracked={pid:root[1]}
 for _ in range(8):
  for child,start in list(tracked.items()):
   identity=process_identity(child)
   if identity and identity[1]==start:
    try:os.kill(child,signal.SIGSTOP)
    except ProcessLookupError:pass
  added=False
  for path in Path('/proc').glob('[0-9]*'):
   child=int(path.name);identity=process_identity(child)
   if identity and identity[0] in tracked and child not in tracked:tracked[child]=identity[1];added=True
  if not added:break
 for child,start in reversed(list(tracked.items())):
  identity=process_identity(child)
  if identity and identity[1]==start:
   try:os.kill(child,signal.SIGKILL)
   except ProcessLookupError:pass
 Path(evidence_path).write_text(json.dumps({'root_pid':pid,'terminated_pid_starttime_identities':tracked,'reason':'Own qualification descendants, including independently sessioned test cases.'},indent=2)+'\n')
