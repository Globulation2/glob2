# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the existing Hive player contract for every client toolchain."""


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

