"""Development-only build identity shared by native, browser and mobile targets."""

DEVELOPMENT_KEYS = ('dev_fast', 'linker', 'web_variant', 'compile_profile')


def development_identity(arguments, target, release=False, profile=False):
    from build_layout import enabled
    result = {}
    for name in ('pch', 'unity'):
        if name in arguments:
            raise ValueError(name + '=1 was removed with the experimental precompiled-header and unity builds')
    if str(arguments.get('dev_fast', 0)).lower() not in ('0', '1', 'false', 'true', 'no', 'yes', 'off', 'on'):
        raise ValueError('dev_fast must be 0 or 1')
    if enabled(arguments.get('dev_fast', 0)):
        result['dev_fast'] = True
    if result and (release or profile):
        raise ValueError('dev_fast requires a non-release, non-profile build')
    linker = arguments.get('linker', 'default')
    if linker not in ('default', 'auto', 'lld'):
        raise ValueError('linker must be default, auto or lld')
    if linker != 'default':
        result['linker'] = linker
    variant = arguments.get('web_variant', 'both')
    if variant not in ('both', 'serial', 'threaded'):
        raise ValueError('web_variant must be both, serial or threaded')
    if variant != 'both':
        if target != 'web' or release:
            raise ValueError('single web_variant requires a browser development build')
        result['web_variant'] = variant
    # compile_profile=core leaves optional sources out (buildsys/sources.py); the full
    # profile records nothing, so its identity and build directory stay unchanged.
    compile_profile = arguments.get('compile_profile', 'full')
    if compile_profile not in ('full', 'core'):
        raise ValueError('compile_profile must be full or core')
    if compile_profile != 'full':
        if target != 'native' or release or profile:
            raise ValueError('compile_profile=core is a native development build; release, profile, web and mobile builds use the full profile')
        result['compile_profile'] = compile_profile
    jobs = str(arguments.get('dependency_jobs', '1'))
    if not jobs.isdecimal() or int(jobs) < 1:
        raise ValueError('dependency_jobs must be a positive integer')
    return result


def dependency_identity(identity):
    return {key: value for key, value in identity.items() if key not in DEVELOPMENT_KEYS and key not in ('client_profile', 'distribution', 'client_features')}

