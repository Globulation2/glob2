"""Localhost and outbound-only SSH use the identical worker RPC protocol."""
import io
import importlib.resources
import json
from pathlib import Path
import os
import shlex
import subprocess
import sys
import zipfile
from .bundles import package_identity
from .common import atomic_bytes, canonical, file_hash
from .model import PROTOCOL_VERSION


def worker_archive(path):
    root = importlib.resources.files(__package__)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('__main__.py', 'import sys\nif len(sys.argv)>1 and sys.argv[1] in (\"rpc\",\"daemon\",\"execute\",\"pack\"):\n from tournaments.worker import main\nelse:\n from tournaments.__main__ import main\nmain()\n')
        for source in sorted((p for p in root.iterdir() if p.name.endswith('.py')), key=lambda p: p.name):
            archive.writestr('tournaments/' + source.name, source.read_bytes())
    atomic_bytes(path, buffer.getvalue())


class Transport:
    def __init__(self, config, package_path):
        self.config = config
        self.name = config['name']
        self.local = config.get('transport', 'ssh') == 'local'
        self.package_id = package_identity()
        self.package_path = Path(package_path).resolve()
        self.root = str(Path(config['directory']) / 'workers' / self.package_id)
        if not Path(self.root).is_absolute():
            raise ValueError('worker directory must be absolute')
        self.remote_package = str(Path(config['directory']) / 'packages' / (self.package_id + '.pyz'))
        self.python = config.get('python', sys.executable if self.local else 'python3')
        self.timeout = config.get('transport_timeout_seconds', 30)
        self.deployed = False

    def command(self, args):
        if self.local:
            return args
        target = self.config.get('ssh', self.name)
        if target.startswith('-') or any(c.isspace() for c in target):
            raise ValueError('SSH target must be a host alias, not options')
        return ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=2',
                target, shlex.join(args)]

    def call(self, args, payload=b''):
        reply = subprocess.run(self.command(args), input=payload, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=self.timeout)
        if reply.returncode:
            raise ConnectionError(reply.stderr.decode(errors='replace')[-4000:])
        return reply.stdout

    def deploy(self):
        script = '''import hashlib, os, pathlib, sys, tempfile
p=pathlib.Path(sys.argv[1]); expected=sys.argv[2]
if p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==expected:
    sys.stdin.buffer.read(); sys.exit(0)
data=sys.stdin.buffer.read()
if hashlib.sha256(data).hexdigest()!=expected: raise ValueError("worker package checksum mismatch")
p.parent.mkdir(parents=True,exist_ok=True)
fd,tmp=tempfile.mkstemp(dir=p.parent)
with os.fdopen(fd,"wb") as f: f.write(data); f.flush(); os.fsync(f.fileno())
os.replace(tmp,p)
'''
        self.call([self.python, '-c', script, self.remote_package, file_hash(self.package_path)], self.package_path.read_bytes())
        self.deployed = True
        config = {k: v for k, v in self.config.items() if k in
                  ('slots', 'collect_slots', 'disk_reserve_bytes', 'spool_budget_bytes', 'cache_budget_bytes', 'builds', 'memory_mb')}
        self.rpc('configure', values=config)
        self.rpc('start')

    def rpc(self, operation, **args):
        if not self.deployed:
            self.deploy()
        request = {'protocol_version': PROTOCOL_VERSION, 'package_id': self.package_id,
                   'op': operation, 'args': args}
        output = self.call([self.python, self.remote_package, 'rpc', self.root], canonical(request))
        try:
            result = json.loads(output)
        except ValueError as error:
            raise ConnectionError('worker emitted invalid protocol JSON') from error
        if not result['ok']:
            raise ValueError(result['error'])
        return result['value']
