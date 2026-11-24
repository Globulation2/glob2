# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the existing Hive player contract for every client toolchain."""


def player_contract(text):
    """Exclude online orchestration from the shared synchronous player API."""
    shared, marker, online = text.partition('## Online program management\n')
    _, player_marker, player = online.partition('## Player API\n')
    if not marker or not player_marker:
        raise ValueError('Hive contract must separate online management and player API sections')
    return shared.rstrip() + '\n\n## Player API\n' + player


def _raw(name, text, delimiter):
    if ')' + delimiter + '"' in text:
        raise ValueError(name + ' contains its C++ raw-string delimiter')
    return ('inline constexpr const char *' + name + ' = R"' + delimiter + '(' + text + ')'
            + delimiter + '";\n')


def render(text, declarations=''):
    """The player contract, and the TypeScript declarations (hive.d.ts), as compiled
    strings: release builds serve both through hive_api without the source tree."""
    text = player_contract(text)
    return ('// Generated from commander-api.txt and examples/hive-mind/hive.d.ts; do not edit.\n'
            '#pragma once\nnamespace Hive {\n'
            + _raw('PlayerAPIContract', text, 'GLOB2_API')
            + _raw('PlayerAPIDeclarations', declarations, 'GLOB2_TYPES')
            + '}\n')

