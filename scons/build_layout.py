"""Build identities: no host probes and no persistent global option state."""
from pathlib import Path
import json
import os
import platform
import tempfile


def enabled(value):
    return str(value).lower() in ('1', 'true', 'yes', 'on')


def build_identity(arguments, host=None):
    target = arguments.get('target', 'native')
    if target not in ('native', 'web', 'android', 'ios'):
        raise ValueError('target must be native, web, android, or ios')
    role = arguments.get('role', 'server' if enabled(arguments.get('server', 0)) else 'client')
    if role not in ('client', 'server', 'router', 'gateway'):
        raise ValueError('role must be client, server, router, or gateway')
    if target in ('android', 'ios'):
        if role != 'client' or enabled(arguments.get('mingw', 0)) or enabled(arguments.get('mingwcross', 0)):
            raise ValueError('mobile targets support only the client role and their own cross compiler')
        if enabled(arguments.get('profile', 0)):
            raise ValueError('mobile profiling uses platform sampling tools; use release=1 with symbols')
        architecture = arguments.get('arch', 'arm64-v8a' if target == 'android' else 'arm64')
        valid = ('arm64-v8a', 'armeabi-v7a', 'x86_64') if target == 'android' else ('arm64',)
        if architecture not in valid:
            raise ValueError(f'{target} arch must be one of {", ".join(valid)}')
        environment = arguments.get('environment', 'device')
        if environment not in ('device', 'simulator') or (target == 'android' and environment != 'device'):
            raise ValueError('environment must be device, or simulator for iOS')
        if target == 'android':
            api = str(arguments.get('api', '26'))
            if not api.isdecimal() or int(api) < 26:
                raise ValueError('Android api must be an integer >= 26')
        else:
            api = str(arguments.get('deployment', '15.0'))
            import re
            if not re.fullmatch(r'\d+\.\d+', api) or tuple(map(int, api.split('.'))) < (15, 0):
                raise ValueError('iOS deployment must be a major.minor version >= 15.0')
        return {'target': target, 'role': role, 'toolchain': target,
                'mode': 'release' if enabled(arguments.get('release', 0)) else 'debug',
                'arch': architecture, 'environment': environment, 'api': api}
    if target == 'web' and (role != 'client' or enabled(arguments.get('mingw', 0)) or enabled(arguments.get('mingwcross', 0))):
        raise ValueError('web supports only the client role and cannot use a native cross compiler')
    toolchain = 'mingwcross' if enabled(arguments.get('mingwcross', 0)) else ('mingw' if enabled(arguments.get('mingw', 0)) else (host or platform.system().lower()))
    if target == 'web':
        toolchain = 'emscripten'
    mode = 'profile' if enabled(arguments.get('profile', 0)) else ('release' if enabled(arguments.get('release', 0)) else 'debug')
    return {'target': target, 'role': role, 'toolchain': toolchain, 'mode': mode}


def default_directory(identity):
    if identity['target'] in ('android', 'ios'):
        return (Path('build') / identity['toolchain'] / identity['environment'] /
                identity['arch'] / identity['api'] / identity['role'] / identity['mode'])
    return Path('build') / identity['toolchain'] / identity['role'] / identity['mode']


def write_if_changed(path, content):
    path = Path(path)
    if path.exists() and path.read_text() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.')
    try:
        with os.fdopen(fd, 'w') as output:
            output.write(content)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def prepare_directory(path, identity):
    path = Path(path)
    path.mkdir(parents=True, exist_ok=True)
    marker = path / 'identity.json'
    content = json.dumps(identity, sort_keys=True, indent=2) + '\n'
    # Exclusive creation prevents incompatible simultaneous configurations sharing outputs.
    try:
        with marker.open('x') as output:
            output.write(content)
    except FileExistsError:
        if marker.read_text() != content:
            raise ValueError(f'{path} belongs to another build configuration; choose a different --build directory')
    return path


class BuildLock:
    """Serialize writers of one identity while allowing other identities to build."""
    def __init__(self, directory):
        self.file = (Path(directory) / '.build-lock').open('a+b')
        try:
            if os.name == 'nt':
                import msvcrt
                self.file.seek(0)
                if not self.file.read(1):
                    self.file.write(b'0')
                    self.file.flush()
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.file.close()
            raise ValueError(f'Another build is using {directory}; wait or choose a different --build directory') from None

    def close(self):
        if not self.file.closed:
            self.file.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
