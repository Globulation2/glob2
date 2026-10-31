"""Explicit native Linux recording variants for distribution packages."""

MODES = ('bundled', 'system', 'disabled')
CODEC_SOURCES = frozenset(('GameplayRecording.cpp',
                           'RecordingEncoder.cpp', 'RecordingFiles.cpp', 'RecordingSession.cpp'))


def resolve(arguments, target, role, toolchain):
    mode = arguments.get('recording', 'bundled')
    if mode not in MODES:
        raise ValueError('recording must be bundled, system, or disabled')
    if mode != 'bundled' and (target != 'native' or role != 'client' or toolchain != 'linux'):
        raise ValueError('recording=system/disabled requires a native Linux client')
    return mode


def sources(entries, mode):
    if mode != 'disabled':
        return list(entries)
    # Renderer capture tickets also use the codec-free Context/JSON helpers in
    # RecordingMetadata.cpp. Keep those definitions even in unoptimized builds,
    # where the unavailable-recording branch is not eliminated by the compiler.
    return [source for source in entries if source not in CODEC_SOURCES] + ['RecordingDisabled.cpp']


def attach_system(env):
    # FFmpeg owns its encoder dependencies (including libx264). Do not link a
    # second codec implementation or look for the private pinned prefix.
    env.ParseConfig('pkg-config --cflags --libs libavformat libavcodec libswscale libswresample libavutil')
