"""Browser editions: one switch per optional feature, and presets over them.

The standard edition builds everything and records nothing in the build identity,
so its directories, caches and generated headers stay as they were. `edition=lite`
is the download-size preset (docs/browser/editions.md): it turns every switch off,
encodes lossy images at a lower quality and links for size. Each switch can be
overridden on either edition (`feature_editor=1`, `webp_quality=75`, ...), so the
cost of every feature can be measured on its own; a build that matches no preset
gets its own directory.

Compile switches leave the feature's sources out of the binary and set
GLOB2_HAS_<NAME> to 0 in glob2/BuildConfig.h (libs/gag/include/BuildFeatures.h has
the defaults). Asset switches leave packages out of the browser's data. None of
them changes what the simulation computes; a build without an AI or generator
simply cannot start or load a game that uses it.
"""

# Every switch but hd_assets also removes code or interface; the browser-only
# switches (WEB_FEATURES) stay on in native builds.
COMPILE_FEATURES = (
    'editor',             # the map and campaign editors
    'online',             # the online platform client: accounts, rooms, rated play, match pages
    'lan',                # LAN games
    'mcp',                # the MCP automation server and its settings
    'hive',               # Hive Mind assistant programs (and the commander HUD)
    'recording',          # gameplay video recording and the recordings browser
    'video_assets',       # H.264 sprite groups and their decoder worker
    'ai_maxima',          # the Maxima AI
    'renderer_gl',        # the programmable and legacy GL (WebGL2) renderers and their shaders
    'renderer_software',  # the software renderer
    'renderer_webgpu',    # the WebGPU renderer
    'js_toolkit',         # JavaScript AIs, the generator toolkit and the generator studio (map
                          # and campaign scripts keep QuickJS)
    'cli',                # the command-line tools (map, match and asset commands, headless play)
    'harfbuzz',           # complex-script text shaping (SDL3_ttf with HarfBuzz)
    'sdl_extras',         # SDL code the game never calls: JPEG/SVG/stb image loaders, SDL's
                          # software and GPU renderers
)
ASSET_FEATURES = (
    'hd_assets',          # high-resolution artwork (hd, hd-terrain-* packages)
    'music_sets',         # soundtrack sets beyond the original, and the music library
    'skins',              # unit skins and their selection
    'translations',       # every language but English (and the CJK font), and the language choice
    'premade_maps',       # the premade map library (random maps and campaigns stay)
    'unused_files',       # data the browser never reads: custom cursors (it uses the system
                          # cursor), shell completions, the menu colony still image
    'page_font',          # the launcher's own font (the page then uses the browser's)
)
FEATURES = COMPILE_FEATURES + ASSET_FEATURES
DEFAULT_WEBP_QUALITY = 90
# Switches only the browser build has; native editions keep them standard.
WEB_FEATURES = ('hd_assets', 'video_assets', 'renderer_gl', 'renderer_software', 'renderer_webgpu',
                'harfbuzz', 'sdl_extras', 'premade_maps', 'unused_files', 'page_font')
# Browser data and link settings: name -> (standard, lite, kind). Kinds: 'int' within
# its range, 'bool', or a tuple of the allowed values.
SETTINGS = {
    'webp_quality': (DEFAULT_WEBP_QUALITY, 70, (1, 100)),   # lossy WebP quality
    'web_opt': ('O3', 'O3', ('O3', 'Os', 'Oz')),            # compile and link optimization (lite keeps speed)
    'web_assertions': (True, False, 'bool'),                # Emscripten runtime assertions
    'web_closure': (False, False, 'bool'),                  # Closure-compile the JavaScript glue
    'music_bitrate': (0, 24, (0, 48)),                      # kbps of the checked-in music copies; 0 the originals
    'sheet_sprites': (False, True, 'bool'),                 # pack terrain decor and small families into sheets
    'variant_blocks': (0, 1, (0, 4)),                       # terrain variant blocks kept; 0 all
    'native_stones': (False, True, 'bool'),                 # stone maps at native resolution (no HD art)
    'terrain_fallbacks': (False, True, 'bool'),             # draw materials with their small_build_fallback
    'single_colour_decor': (False, True, 'bool'),           # one corner colour, no mass-meets-mass decor frames
}
WEB_SETTINGS = tuple(SETTINGS)

STANDARD = dict(features={name: True for name in FEATURES}, **{key: spec[0] for key, spec in SETTINGS.items()})
PRESETS = {
    'standard': STANDARD,
    'lite': dict(features={name: False for name in FEATURES}, **{key: spec[1] for key, spec in SETTINGS.items()}),
}


def _boolean(name, value):
    value = str(value).lower()
    if value not in ('0', '1', 'true', 'false', 'yes', 'no', 'on', 'off'):
        raise ValueError(name + ' must be a boolean')
    return value in ('1', 'true', 'yes', 'on')


def resolve(arguments, target):
    """The edition settings for build arguments; {} for an unchanged standard build."""
    name = arguments.get('edition', 'standard')
    if name not in PRESETS:
        raise ValueError('edition must be ' + ' or '.join(PRESETS))
    preset = PRESETS[name]
    features = dict(preset['features'])
    for feature in FEATURES:
        key = 'feature_' + feature
        if key in arguments:
            features[feature] = _boolean(key, arguments[key])
    settings = dict(features=features)
    for key, (_, _, kind) in SETTINGS.items():
        value = arguments.get(key, preset[key])
        if kind == 'bool':
            value = _boolean(key, value)
        elif isinstance(kind[0], int):
            text = str(value)
            if not text.isdecimal() or not kind[0] <= int(text) <= kind[1]:
                raise ValueError(f'{key} must be an integer from {kind[0]} to {kind[1]}')
            value = int(text)
        elif value not in kind:
            raise ValueError(key + ' must be ' + ', '.join(kind))
        settings[key] = value
    if target != 'web':
        # Native builds take the compile switches only, so the gated code keeps
        # compiling where the fast gate builds; the browser-only settings stay standard.
        for key in WEB_SETTINGS:
            if key in arguments:
                raise ValueError(key + ' requires target=web')
            settings[key] = STANDARD[key]
        for feature in WEB_FEATURES:
            if 'feature_' + feature in arguments:
                raise ValueError('feature_' + feature + ' requires target=web')
            features[feature] = True
    if settings == STANDARD:
        return {}
    return {'edition': dict(name=name, **settings)}


def settings(identity):
    """The full edition settings of a build identity (the standard ones when it records none)."""
    edition = identity.get('edition')
    if edition is None:
        return dict(name='standard', **STANDARD)
    return edition


def enabled(identity, feature):
    return settings(identity)['features'][feature]


def client_features(identity, features):
    """buildsys/client_features.py capabilities an edition's switches rule out: the
    commander is Hive Mind's HUD; community AIs, generators and authoring links
    are online services."""
    features = dict(features)
    if not enabled(identity, 'hive'):
        features['commander'] = False
    if not enabled(identity, 'online'):
        features.update(community_ai=False, community_generators=False, authoring_links=False)
    return features


def page_settings(identity):
    """What platforms/web/shell.html reads as globalThis.Glob2Edition."""
    edition = settings(identity)
    return dict(name=edition['name'], features={name: edition['features'][name] for name in FEATURES})


def directory_name(identity):
    """'' for the standard edition, else the preset's name, followed by any
    differences from that preset (lite-with_editor, edition-no_hd_assets, ...)."""
    edition = identity.get('edition')
    if edition is None:
        return ''
    preset = PRESETS[edition['name']]
    words = []
    native = identity.get('target') != 'web'
    for feature in FEATURES:
        if native and feature in WEB_FEATURES:
            continue  # always on natively
        if edition['features'][feature] != preset['features'][feature]:
            words.append(('with_' if edition['features'][feature] else 'no_') + feature)
    for key in WEB_SETTINGS:
        if edition[key] != preset[key] and not native:
            words.append(key + '_' + str(edition[key]).lower())
    stem = 'edition' if edition['name'] == 'standard' else edition['name']
    return '-'.join([stem] + words)


def header(identity):
    """glob2/BuildConfig.h lines; nothing for the standard edition (BuildFeatures.h
    defaults every switch on), so its generated header is unchanged."""
    edition = identity.get('edition')
    if edition is None:
        return ''
    lines = ['#define GLOB2_EDITION "%s"' % edition['name']]
    for feature in FEATURES:
        lines.append('#define GLOB2_HAS_%s %d' % (feature.upper(), int(edition['features'][feature])))
    return '\n'.join(lines) + '\n'


def excluded_source_groups(identity):
    """Names of the buildsys/sources.py FEATURE_SOURCES groups this build leaves out."""
    return [feature for feature in COMPILE_FEATURES if not enabled(identity, feature)]
