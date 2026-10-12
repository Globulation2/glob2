"""Optional ccache wrapper for the C/C++ compilers.

Enabled by setting CCACHE=1 in the environment. An env var rather than a scons
option so it never lands in options_cache.py and silently sticks across later
builds. Note that compile_commands.json records the wrapped command line, so
leave CCACHE unset when regenerating it for tools/remove-unused-includes.py.
"""

import os
import shutil
import shlex

# ccache settings forwarded into the environment scons scrubs for build
# commands. Deliberately absent: CCACHE_SLOPPINESS. Marking include_file_mtime
# or include_file_ctime sloppy makes ccache trust timestamps over content, and
# time_macros would let it cache the __DATE__/__TIME__ build banner in
# GlobalContainerArgs.cpp. Both trade correctness for hit rate.
_FORWARDED = ('CCACHE_COMPILERCHECK', 'CCACHE_MAXSIZE',
              'CCACHE_DISABLE', 'CCACHE_LOGFILE', 'CCACHE_DEBUG', 'CCACHE_DEBUGDIR', 'HOME')


def enabled():
    return os.environ.get('CCACHE', '').lower() not in ('', '0', 'false', 'no', 'off')


def enable(env):
    """Prefix compilation commands with ccache once CC and CXX are final."""
    binary = shutil.which('ccache')
    if not binary:
        raise SystemExit("CCACHE is set but ccache was not found on PATH")
    from pathlib import Path
    import sys
    if str(Path(__file__).resolve().parent) not in sys.path:
        sys.path.append(str(Path(__file__).resolve().parent))
    from dev_store import cache, adopt
    env['ENV']['CCACHE_DIR'] = str(adopt(os.environ['CCACHE_DIR'])) if os.environ.get('CCACHE_DIR') else str(cache(Path.cwd(), 'ccache'))
    env['ENV']['CCACHE_MAXSIZE'] = os.environ.get('CCACHE_MAXSIZE', '4G')
    # Wrap compilation commands, not CC/CXX: SMARTLINK and configure link
    # probes must keep the original compiler driver without a ccache process.
    # Respect callers who already supplied a ccache-prefixed compiler.
    for compiler, commands in (('CC', ('CCCOM', 'SHCCCOM')),
                               ('CXX', ('CXXCOM', 'SHCXXCOM'))):
        words = shlex.split(str(env.get(compiler, '')))
        if words and os.path.basename(words[0]) == 'ccache':
            continue
        for command in commands:
            value = env.get(command)
            if value and not str(value).startswith(binary + ' '):
                env[command] = binary + ' ' + str(value)
    from dev_build import cache_flags
    cache_flags(env)
    for var in _FORWARDED:
        if var in os.environ:
            env['ENV'][var] = os.environ[var]
