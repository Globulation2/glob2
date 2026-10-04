"""Build identities: no host probes and no persistent global option state."""
from pathlib import Path
import json
import os
import platform
import tempfile


PACKAGE_VERSION = "0.10.0.1"

def enabled(value):
    return str(value).lower() in ('1', 'true', 'yes', 'on')


def build_identity(arguments, host=None):
    size_profile = arguments.get('size_optimization', 'none')
    if size_profile not in ('none', 'gc', 'lto', 'size'):
        raise ValueError('size_optimization must be none, gc, lto, or size')
    target = arguments.get('target', 'native')
    if size_profile != 'none' and target != 'native':
        raise ValueError('size_optimization experiments require a native release build')
    if target not in ('native', 'web', 'android', 'ios'):
        raise ValueError('target must be native, web, android, or ios')
    # server=0 is still accepted so existing client commands keep working.
    if enabled(arguments.get('server', 0)):
        raise ValueError('server=1 (the YOG lobby server) was removed; build role=client or role=relay')
    role = arguments.get('role', 'client')
    if role not in ('client', 'relay'):
        raise ValueError('role must be client or relay')
    china = enabled(arguments.get('china', 0))
    if china and (target == 'web' or role != 'client'):
        raise ValueError('china=1 supports native and mobile clients only')
    amazon = enabled(arguments.get('amazon', 0))
    if amazon and (target != 'android' or role != 'client' or china or not enabled(arguments.get('release', 0))):
        raise ValueError('amazon=1 supports only the standard Android release client')
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
            api = str(arguments.get('api', '24'))
            if not api.isdecimal() or int(api) < 24:
                raise ValueError('Android api must be an integer >= 24')
        else:
            api = str(arguments.get('deployment', '15.0'))
            import re
            if not re.fullmatch(r'\d+\.\d+', api) or tuple(map(int, api.split('.'))) < (15, 0):
                raise ValueError('iOS deployment must be a major.minor version >= 15.0')
        identity = {'target': target, 'role': role, 'toolchain': target,
                'mode': 'release' if enabled(arguments.get('release', 0)) else 'debug',
                'arch': architecture, 'environment': environment, 'api': api}
        if china:
            identity['china'] = True
        if amazon:
            identity['amazon'] = True
        return identity
    if target == 'web' and (role != 'client' or enabled(arguments.get('mingw', 0)) or enabled(arguments.get('mingwcross', 0))):
        raise ValueError('web supports only the client role and cannot use a native cross compiler')
    toolchain = 'mingwcross' if enabled(arguments.get('mingwcross', 0)) else ('mingw' if enabled(arguments.get('mingw', 0)) else (host or platform.system().lower()))
    if target == 'web':
        toolchain = 'emscripten'
    mode = 'profile' if enabled(arguments.get('profile', 0)) else ('release' if enabled(arguments.get('release', 0)) else 'debug')
    native_wss = target == 'native' and role == 'client' and enabled(arguments.get('wss', 1))
    identity = {'target': target, 'role': role, 'toolchain': toolchain, 'mode': mode,
                'native_wss': native_wss}
    if size_profile != 'none':
        if mode != 'release' or toolchain not in ('linux', 'mingw'):
            raise ValueError('size_optimization experiments require a Linux or MinGW release build')
        identity['size_optimization'] = size_profile
    if enabled(arguments.get('lean_images', 0)):
        if target != 'native' or toolchain not in ('linux', 'mingw') or role != 'client' or not enabled(arguments.get('release', 0)) or enabled(arguments.get('mingwcross', 0)):
            raise ValueError('lean_images requires a native Linux or MinGW release client')
        identity['lean_images'] = True
    if china:
        identity['china'] = True
    return identity


def default_directory(identity):
    if identity['target'] in ('android', 'ios'):
        path = (Path('build') / identity['toolchain'] / identity['environment'] /
                identity['arch'] / identity['api'] / identity['role'] / identity['mode'])
        if identity.get('china'):
            return path / 'china'
        return path / 'amazon' if identity.get('amazon') else path
    role = identity['role']
    if role == 'client' and identity['target'] == 'native' and not identity['native_wss']:
        role += '-tcp'
    path = Path('build') / identity['toolchain'] / role / identity['mode']
    if identity.get('size_optimization'):
        path /= 'size-' + identity['size_optimization']
    if identity.get('lean_images'):
        path /= 'lean-images'
    return path / 'china' if identity.get('china') else path


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
