#!/usr/bin/env python3
"""Capture only multiplayer ports inside an explicitly selected test container.

Usage: capture_container.py CONTAINER OUTPUT.pcap [SECONDS]
Requires Docker; the temporary capture process shares only that network namespace.
"""
import json
from pathlib import Path
import subprocess
import sys

container, destination = sys.argv[1:3]
seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 30
image = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]['Config']['Image']
script = r'''
import socket,struct,sys,time
s=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3)); s.settimeout(.5)
out=sys.stdout.buffer
out.write(struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,65535,1));out.flush()
deadline=time.monotonic()+int(sys.argv[1])
while time.monotonic()<deadline:
 try: packet=s.recv(65535)
 except TimeoutError: continue
 if len(packet)<34 or packet[12:14]!=b'\x08\x00': continue
 ip=14; protocol=packet[ip+9]; start=ip+(packet[ip]&15)*4
 if len(packet)<start+4: continue
 ports=struct.unpack('!HH',packet[start:start+4])
 if not (protocol==6 and any(p in (7489,7490,7491) for p in ports) or protocol==17 and 7486 in ports): continue
 now=time.time(); size=len(packet)
 out.write(struct.pack('<IIII',int(now),int(now%1*1000000),size,size));out.write(packet);out.flush()
'''
Path(destination).parent.mkdir(parents=True, exist_ok=True)
with open(destination, 'wb') as output:
    subprocess.run(['docker', 'run', '--rm', '--user', '0:0', '--cap-drop', 'ALL', '--cap-add', 'NET_RAW',
        '--network', 'container:'+container, image, 'python3', '-uc', script, str(seconds)], stdout=output, check=True)
