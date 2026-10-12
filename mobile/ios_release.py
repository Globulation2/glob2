"""App Store marketing version encoding for the shared four-component version."""
import re


def marketing_version(version):
    if not re.fullmatch(r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)', version):
        raise ValueError('iOS requires a four-component game version')
    major,minor,patch,revision=map(int,version.split('.'))
    if revision>=100:
        raise ValueError('iOS version revision must be below 100 to prevent collisions')
    return f'{major}.{minor}.{100*patch+revision}'
