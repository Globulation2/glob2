"""The official platform instance, as one build-time setting.

Every build path (desktop SConstruct, web, Android, iOS) takes the origin from here,
or from the `official_instance=https://host` SCons argument, so moving the official
instance to another domain is a change to DEFAULT_ORIGIN alone. It feeds the client
constant `Online::OFFICIAL_INSTANCE_ORIGIN` (src/online/InstanceConfig.h), the Android
App Link host and the iOS associated domain. Legacy YOG endpoints are separate.
"""

from urllib.parse import urlsplit

DEFAULT_ORIGIN = 'https://app.glob2online.com'


def origin(arguments=None):
    value = (arguments or {}).get('official_instance', DEFAULT_ORIGIN).rstrip('/')
    parts = urlsplit(value)
    if parts.scheme != 'https' or not parts.hostname or parts.path or parts.query or parts.username:
        raise ValueError('official_instance must be an https origin such as https://example.org, not ' + repr(value))
    return value.lower()


def host(value):
    return urlsplit(value).hostname


def cppdefine(value):
    """The preprocessor definition carrying the origin as a string literal."""
    return ('GLOB2_OFFICIAL_INSTANCE_ORIGIN', '\\"%s\\"' % value)
