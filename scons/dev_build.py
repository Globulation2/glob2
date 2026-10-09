"""Development-only build policy shared by native, browser and mobile targets."""
import os
from pathlib import Path
import shutil

DEVELOPMENT_KEYS = ('dev_fast', 'pch', 'unity', 'linker', 'web_variant')


def development_identity(arguments, target, release=False, profile=False):
    from build_layout import enabled
    result = {}
    for name in ('dev_fast', 'pch', 'unity'):
        if str(arguments.get(name, 0)).lower() not in ('0', '1', 'false', 'true', 'no', 'yes', 'off', 'on'):
            raise ValueError(name + ' must be 0 or 1')
        if enabled(arguments.get(name, 0)):
            result[name] = True
    if result and (release or profile):
        raise ValueError('dev_fast, pch and unity require a non-release, non-profile build')
    linker = arguments.get('linker', 'default')
    if linker not in ('default', 'auto', 'lld'):
        raise ValueError('linker must be default, auto or lld')
    if linker != 'default':
        result['linker'] = linker
    variant = arguments.get('web_variant', 'both')
    if variant not in ('both', 'serial', 'threaded'):
        raise ValueError('web_variant must be both, serial or threaded')
    if variant != 'both':
        if target != 'web' or release:
            raise ValueError('single web_variant requires a browser development build')
        result['web_variant'] = variant
    jobs = str(arguments.get('dependency_jobs', '1'))
    if not jobs.isdecimal() or int(jobs) < 1:
        raise ValueError('dependency_jobs must be a positive integer')
    return result


def dependency_identity(identity):
    return {key: value for key, value in identity.items() if key not in DEVELOPMENT_KEYS}


def can_build_dependencies():
    from SCons.Script import GetOption, COMMAND_LINE_TARGETS
    targets = list(COMMAND_LINE_TARGETS)
    database_only = bool(targets) and all(Path(str(target)).name == 'compile_commands.json' for target in targets)
    return not any(GetOption(name) for name in ('clean', 'no_exec', 'help')) and not database_only


def dependency_jobs(arguments):
    from SCons.Script import GetOption
    return int(arguments.get('dependency_jobs', GetOption('num_jobs')))


def configure(env, identity):
    """Apply only application flags; dependency builders keep their own policy."""
    env['DEV_IDENTITY'] = identity
    if identity.get('dev_fast'):
        for name in ('CFLAGS', 'CXXFLAGS', 'CCFLAGS', 'LINKFLAGS'):
            env[name] = [flag for flag in env.Split(env.get(name, []))
                         if not str(flag).startswith(('-g', '-O'))]
        env.Append(CCFLAGS=['-O0', '-g1'])
    choice = identity.get('linker', 'default')
    eligible = identity['target'] == 'native' and identity['toolchain'] == 'linux'
    binary = shutil.which('ld.lld', path=env.get('ENV', {}).get('PATH')) if eligible else None
    if choice == 'lld' and not binary:
        raise ValueError('linker=lld requires native Linux and an installed ld.lld')
    if choice != 'default' and binary:
        env.Append(LINKFLAGS=['-fuse-ld=lld'])


def cache_flags(env):
    """Map debug paths only: __FILE__ and configured runtime paths retain meaning."""
    if not env.get('DEV_IDENTITY', {}).get('dev_fast'):
        return
    root = Path(env.Dir('#').abspath)
    env.Append(CCFLAGS=['-fdebug-prefix-map=' + str(root) + '=.'])
    env['ENV']['CCACHE_BASEDIR'] = str(root)
    env['ENV']['CCACHE_COMPILERCHECK'] = os.environ.get('CCACHE_COMPILERCHECK', 'content')
