"""Shared native/web/mobile build rule for reviewed Bulgarian and Thai errors."""
from pathlib import Path
import subprocess
import sys
from SCons.Action import Action

INPUTS = ('#tools/localization/build_online_error_catalog.py',
          '#platform/packages/i18n/locales/en.json',
          '#platform/packages/i18n/locales/bg.json',
          '#platform/packages/i18n/locales/th.json')
CONSUMERS = ('online/ApiErrorText.cpp', 'online/ApiErrorTextTest.cpp')


def _generate(target, source, env):
    subprocess.check_call([sys.executable, source[0].abspath,
                           '--english', source[1].abspath,
                           '--bulgarian', source[2].abspath,
                           '--thai', source[3].abspath,
                           '--output', target[0].abspath])
    return 0


def configure(env, include_directory):
    """Use the builder's existing include directory; never alter compile flags."""
    root = Path(include_directory).resolve()
    return env.Command(str(root / 'generated/OnlineErrorCatalog.h'), list(INPUTS),
                       Action(_generate, 'Embedding reviewed Bulgarian and Thai API error text'))


def bind_consumer(env, targets, source, catalog):
    """An explicit prerequisite works even before the include exists for scanning."""
    name = str(source).replace('\\', '/').lstrip('#')
    if any(name == suffix or name.endswith('/' + suffix) for suffix in CONSUMERS):
        env.Depends(targets, catalog)


def bind_objects(env, by_source, catalog):
    # Unity members may map to a shared object: attach to that actual object.
    for source, targets in by_source.items():
        bind_consumer(env, targets, source, catalog)
