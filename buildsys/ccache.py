"""The managed ccache store's configuration.

CMakeLists.txt makes ccache the compiler launcher when it is installed
(GLOB2_CCACHE), with this store as CCACHE_DIR.
"""

from pathlib import Path

# The managed cache's one configuration. Every builder (CMake, tools/dev_build.py,
# a bare `ccache -s`) reads it from the cache directory, so limits never disagree.
# CCACHE_MAXSIZE and other CCACHE_* variables in the environment still override.
MANAGED_CONFIG = {'max_size': '20G', 'compiler_check': 'content', 'compression': 'true'}


def configure_managed(directory):
    from build_layout import write_if_changed
    text = ''.join(f'{key} = {value}\n' for key, value in MANAGED_CONFIG.items())
    write_if_changed(Path(directory) / 'ccache.conf', text)

