"""Client product capabilities; these never change simulation or file formats."""

FEATURES = ('commander', 'authoring_links', 'community_ai', 'community_generators')
DISTRIBUTIONS = ('direct', 'browser', 'steam', 'epic', 'google_play', 'app_store',
                 'microsoft', 'amazon', 'china')


def resolve(arguments):
    release = str(arguments.get('release', 0)).lower() in ('1', 'true', 'yes', 'on')
    profile = arguments.get('client_profile', 'free' if release else 'full')
    if profile not in ('full', 'free'):
        raise ValueError('client_profile must be full or free')
    distribution = arguments.get('distribution', {'web': 'browser', 'android': 'google_play',
                                                  'ios': 'app_store'}.get(arguments.get('target'), 'direct'))
    if distribution not in DISTRIBUTIONS:
        raise ValueError('unsupported distribution')
    values = dict(commander=profile == 'full', authoring_links=profile == 'full',
                  community_ai=True, community_generators=True)
    if profile == 'free' and distribution == 'app_store':
        values.update(community_ai=False, community_generators=False)
    for name in FEATURES:
        value = str(arguments.get('feature_' + name, int(values[name]))).lower()
        if value not in ('0', '1', 'true', 'false', 'yes', 'no', 'on', 'off'):
            raise ValueError('feature_' + name + ' must be a boolean')
        values[name] = value in ('1', 'true', 'yes', 'on')
    # Existing restricted editions take precedence over explicit overrides.
    if any(str(arguments.get(name, 0)).lower() in ('1', 'true', 'yes', 'on') for name in ('china', 'amazon')):
        values.update(commander=False, authoring_links=False)
    return {'client_profile': profile, 'distribution': distribution, 'client_features': values}


def header(identity):
    features = identity.get('client_features', resolve({})['client_features'])
    return ''.join('#define GLOB2_FEATURE_' + name.upper() + ' ' + str(int(features[name])) + '\n'
                   for name in FEATURES)


def directory_suffix(identity):
    # Keep standard packaging paths stable; explicit variants get isolated outputs.
    if identity.get('role') != 'client' or 'client_features' not in identity:
        return ''
    default = resolve({'target': identity['target'], 'release': identity['mode'] == 'release',
                       'china': identity.get('china', False), 'amazon': identity.get('amazon', False)})
    actual = {key: identity[key] for key in default}
    if actual == default:
        return ''
    bits = ''.join(str(int(identity['client_features'][name])) for name in FEATURES)
    return 'client-' + identity['client_profile'] + '-' + identity['distribution'] + '-' + bits
