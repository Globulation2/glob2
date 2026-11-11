"""Native harness and tool programs src/SConscript builds from the client objects.

Shared by src/SConscript and the CMake build
(scons/export_cmake_sources.py), so both build the same programs from the same
objects. Each program compiles one harness source with the client's flags and
links it with:

  CLIENT          every client object except the entry point, then the libraries
  CLIENT_OBJECTS  the harness object first, then the client objects and libraries
  LIBRARIES       libgag and libusl only
  a tuple         those client sources' objects (paths relative to src/)

Fields: `object` names the harness object (default: the source's basename);
`cxxflags` are appended for the harness object only; `shared` names objects of
SHARED_OBJECTS linked after it; `trust` adds the platform trust objects (none on
Linux); `require` is a build feature the program needs ('wss'); `profile` is the
scons/sources.py optional group it drives (compile_profile=core leaves it out).
"""
from collections import namedtuple

CLIENT = 'client'
CLIENT_OBJECTS = 'client-objects'
LIBRARIES = 'libraries'

Harness = namedtuple('Harness', 'target alias source link object cxxflags shared trust require profile',
                     defaults=(None, (), (), False, None, None))

_NET = ('net/NetTransport.cpp', 'net/TcpTransport.cpp', 'net/WssTransport.cpp', 'net/NetworkConfig.cpp',
        'net/LanIdentity.cpp')
_PLATFORM = ('online/HttpFetch.cpp', 'online/HttpFetchCommon.cpp', 'online/InstanceConfig.cpp',
             'online/MemoryStorage.cpp', 'online/PlatformClient.cpp', 'online/PlatformProtocol.cpp')
_NO_ACCESS_CONTROL = ('-fno-access-control',)

# One object linked into several programs: name -> (object, source).
SHARED_OBJECTS = {
    'OnlineProbeFileManager': ('OnlineProbeFileManager.o', '#src/online/OnlineProbeFileManager.cpp'),
}

HARNESS_PROGRAMS = (
    Harness('LANSessionHarness', 'lan-test', '#src/net/lan/LANSessionHarness.cpp', CLIENT),
    # It reads the engine's game state to report what it sees.
    Harness('OnlinePlayHarness', 'online-play-test', '#src/online/OnlinePlayHarness.cpp', CLIENT,
            cxxflags=_NO_ACCESS_CONTROL),
    # Twelve-team sprite benchmark; not part of the default build.
    Harness('twelve-team-benchmark', 'twelve-team-benchmark', '#tools/unit-animation/twelve-team-benchmark.cpp',
            LIBRARIES, object='TwelveTeamBenchmark.o'),
    # Map analysis tools and the golden test use production generators and widgets.
    Harness('MapGeneratorStudy', 'map-generator-study', '#src/map/generator/MapGeneratorStudy.cpp', CLIENT,
            profile='generators'),
    Harness('MapGeneratorGoldenTest', 'map-generator-golden-test', '#src/map/generator/MapGeneratorGoldenTest.cpp',
            CLIENT, profile='generators'),
    Harness('MapGeneratorProfileFixture', 'map-generator-profile-fixture',
            '#src/map/generator/MapGeneratorProfileFixture.cpp', CLIENT, profile='generators'),
    Harness('net-connection-test', 'transport-test', '#src/net/NetConnectionHarness.cpp', CLIENT),
    Harness('lan-discovery-test', 'transport-test', '#src/net/LANDiscoveryHarness.cpp',
            ('net/NetBroadcastListener.cpp', 'net/NetBroadcaster.cpp', 'net/NetworkConfig.cpp', 'net/LanIdentity.cpp')),
    Harness('wss-transport-test', 'transport-test', '#src/net/WssTransportHarness.cpp', _NET, trust=True,
            require='wss'),
    Harness('wss-listener-test', 'transport-test', '#src/net/WssListenerHarness.cpp', _NET, trust=True,
            require='wss'),
    Harness('http-fetch-test', 'transport-test', '#src/online/HttpFetchHarness.cpp',
            ('online/HttpFetch.cpp', 'online/HttpFetchCommon.cpp'), trust=True, require='wss'),
    Harness('platform-client-probe', 'transport-test', '#src/online/PlatformClientProbe.cpp',
            _PLATFORM + ('online/SimVersion.cpp', 'online/Sha256.cpp') + _NET,
            shared=('OnlineProbeFileManager',), trust=True, require='wss'),
    Harness('online-screens-probe', 'online-screens-probe', '#src/online/OnlineScreensProbe.cpp',
            _PLATFORM + ('online/OnlineResources.cpp', 'online/QuickMatch.cpp', 'online/RelayProbe.cpp',
                         'online/MapCatalog.cpp', 'online/SimVersion.cpp', 'online/Sha256.cpp') + _NET,
            shared=('OnlineProbeFileManager',), trust=True, require='wss'),
    # Offline production-screen capture for the mobile design review gallery; it drives the editor.
    Harness('mobile-gallery', 'mobile-gallery', '#tools/MobileGalleryHarness.cpp', CLIENT, profile='editor'),
    # Original-derived skin comparison and full game render diagnostics.
    Harness('skin-preview', 'skin-preview', '#tools/skins/preview.cpp', LIBRARIES, object='SkinPreview.o'),
    # Diagnostic harnesses inspect view-owned appearance, as engine render tests do.
    Harness('skin-rig-benchmark', 'skin-rig-benchmark', '#tools/skins/rig_benchmark.cpp', LIBRARIES,
            object='SkinRigBenchmark.o', cxxflags=_NO_ACCESS_CONTROL),
    Harness('skin-game-preview', 'skin-game-preview', '#tools/skins/game_preview.cpp', CLIENT_OBJECTS,
            object='SkinGamePreview.o', cxxflags=_NO_ACCESS_CONTROL),
)


def object_name(program):
    """The harness object's file name in the src build directory."""
    return program.object or program.source.rsplit('/', 1)[1].rsplit('.', 1)[0] + '.o'
