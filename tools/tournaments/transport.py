"""Localhost and outbound-only SSH use the identical worker RPC protocol."""
import io
import importlib.resources
import json
from pathlib import Path
import os
import selectors
import tempfile
import time
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
        archive.writestr('__main__.py', 'import sys\nif len(sys.argv)>1 and sys.argv[1] in (\"rpc\",\"rpc-stream\",\"daemon\",\"execute\",\"pack\"):\n from tournaments.worker import main\nelse:\n from tournaments.__main__ import main\nmain()\n')
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
        self.process = None
        self.errors = None

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
                  ('slots', 'collect_slots', 'result_backlog_limit', 'disk_reserve_bytes', 'spool_budget_bytes', 'cache_budget_bytes', 'builds', 'memory_mb')}
        self.rpc('configure', values=config)
        self.rpc('start')

    def close(self):
        if getattr(self, 'process', None) is not None:
            self.process.kill()
            self.process.wait()
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        if getattr(self, 'errors', None) is not None:
            self.errors.close()
            self.errors = None

    def __del__(self):
        self.close()

    def exchange(self, payload):
        """One bounded, timed request on a persistent SSH/Python stream.

        A Transport belongs to one lane, never shared between concurrent requests.
        Do not retry here: an interrupted mutation may already be committed.
        """
        if len(payload) > 2 * 1024 * 1024:
            raise ValueError('RPC request exceeds bound')
        try:
            if self.process is None:
                self.errors = tempfile.TemporaryFile()
                self.process = subprocess.Popen(
                    self.command([self.python, self.remote_package, 'rpc-stream', self.root]),
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.errors, bufsize=0)
                os.set_blocking(self.process.stdin.fileno(), False)
                os.set_blocking(self.process.stdout.fileno(), False)
            sent, reply = 0, bytearray()
            deadline = time.monotonic() + self.timeout
            with selectors.DefaultSelector() as selector:
                selector.register(self.process.stdin, selectors.EVENT_WRITE)
                selector.register(self.process.stdout, selectors.EVENT_READ)
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired('worker RPC', self.timeout)
                    for key, _ in selector.select(remaining):
                        if key.fileobj is self.process.stdin:
                            sent += os.write(key.fd, payload[sent:sent+65536])
                            if sent == len(payload): selector.unregister(key.fileobj)
                        else:
                            block = os.read(key.fd, 65536)
                            if not block:
                                self.errors.seek(0)
                                raise ConnectionError('worker RPC disconnected: ' + self.errors.read()[-4000:].decode(errors='replace'))
                            reply.extend(block)
                            if len(reply) > 32 * 1024 * 1024:
                                raise ConnectionError('worker RPC response exceeds bound')
                            if reply.endswith(b'\n'):
                                return bytes(reply)
        except BaseException:
            self.close()
            raise

    def rpc(self, operation, **args):
        if not self.deployed:
            self.deploy()
        request = {'protocol_version': PROTOCOL_VERSION, 'package_id': self.package_id,
                   'op': operation, 'args': args}
        output = self.exchange(canonical(request) + b'\n')
        try:
            result = json.loads(output)
        except ValueError as error:
            self.close()
            raise ConnectionError('worker emitted invalid protocol JSON') from error
        if not result['ok']:
            raise ValueError(result['error'])
        return result['value']
