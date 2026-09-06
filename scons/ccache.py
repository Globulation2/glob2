"""Optional ccache wrapper for the C/C++ compilers.

Enabled by setting CCACHE=1 in the environment. An env var rather than a scons
option so it never lands in options_cache.py and silently sticks across later
builds. Note that compile_commands.json records the wrapped command line, so
leave CCACHE unset when regenerating it for tools/remove-unused-includes.py.
"""

import os
import shutil

# ccache settings forwarded into the environment scons scrubs for build
# commands. Deliberately absent: CCACHE_SLOPPINESS. Marking include_file_mtime
# or include_file_ctime sloppy makes ccache trust timestamps over content, and
# time_macros would let it cache the __DATE__/__TIME__ build banner in
# GlobalContainerArgs.cpp. Both trade correctness for hit rate.
_FORWARDED = ('CCACHE_DIR', 'CCACHE_COMPILERCHECK', 'CCACHE_MAXSIZE',
              'CCACHE_DISABLE', 'CCACHE_LOGFILE', 'HOME')


def enabled():
    return bool(os.environ.get('CCACHE'))


def enable(env):
    """Prefix env's compilers with ccache. Call once CC and CXX are final."""
    binary = shutil.which('ccache')
    if not binary:
        raise SystemExit("CCACHE is set but ccache was not found on PATH")
    for var in ('CC', 'CXX'):
        if env.get(var) and not env[var].startswith(binary):
            env[var] = binary + ' ' + env[var]
    for var in _FORWARDED:
        if var in os.environ:
            env['ENV'][var] = os.environ[var]
