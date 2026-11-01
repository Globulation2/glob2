# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the existing Hive player contract for every client toolchain."""
from pathlib import Path


def player_contract(text):
    """Exclude online orchestration from the shared synchronous player API."""
    shared, marker, online = text.partition('## Online program management\n')
    _, player_marker, player = online.partition('## Player API\n')
    if not marker or not player_marker:
        raise ValueError('Hive contract must separate online management and player API sections')
    return shared.rstrip() + '\n\n## Player API\n' + player


def render(text):
    text = player_contract(text)
    delimiter = 'GLOB2_API'
    if ')' + delimiter + '"' in text:
        raise ValueError('Hive contract contains its C++ raw-string delimiter')
    return ('// Generated from commander-api.txt; do not edit.\n#pragma once\n'
            'namespace Hive { inline constexpr const char *PlayerAPIContract = R"' + delimiter
            + '(' + text + ')' + delimiter + '"; }\n')


def configure(env, directory):
    from SCons.Script import Action
    from build_layout import write_if_changed

    def generate(target, source, env):
        write_if_changed(str(target[0]), render(Path(str(source[0])).read_text(encoding='utf-8')))
        return 0

    # SConscript evaluation can run with its variant directory as cwd. Resolve
    # relative build paths against the project, just like the include search path.
    directory = Path(directory)
    if not directory.is_absolute():
        directory = Path(env.Dir('#').abspath) / directory
    target = directory.resolve() / 'include/hive/PlayerAPIContract.h'
    return env.Command(str(target), [env.File('#platform/packages/hive/src/commander-api.txt'), env.File('#scons/hive_api.py')],
                       Action(generate, 'Generating Hive player API contract'))
