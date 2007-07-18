env = Environment()
conf = Configure(env)
env.Append(CPPPATH='#libgag/include')
env.Append(LIBPATH='#libgag/src')
env.Append(CXXFLAGS='-g -pg')
env.Append(LINKFLAGS='-g -pg')
env.Append(LIBS=['SDL', 'SDL_ttf', 'SDL_image', 'SDL_net', 'speex', 'vorbisfile', 'boost_thread'])

if not conf.CheckLib('SDL'):
    print "Could not find libSDL"
    Exit(1)
elif not conf.CheckLib('SDL_ttf'):
    print "Could not find libSDL_ttf"
    Exit(1)
elif not conf.CheckLib('SDL_image'):
    print "Could not find libSDL_image"
    Exit(1)
elif not conf.CheckLib('SDL_net'):
    print "Could not find libSDL_net"
    Exit(1)
elif not conf.CheckLib('speex') or not conf.CheckCHeader('speex/speex.h'):
    print "Could not find libspeex or could not find 'speex/speex.h'"
    Exit(1)
elif not conf.CheckLib('vorbisfile'):
    print "Could not find libvorbisfile to link gainst"
    Exit(1)
elif not conf.CheckLib('z') or not conf.CheckCHeader('zlib.h'):
    print "Could not find zlib.h"
    Exit(1)
elif not conf.CheckLib('boost_thread') or not conf.CheckCXXHeader('boost/thread/thread.hpp'):
    print "Could not find boost_thread or boost/thread/thread.hpp"
    Exit(1)
elif not conf.CheckLib('GL') or not conf.CheckCHeader('GL/gl.h'):
    if not conf.CheckLib('GL') or not conf.CheckCHeader('OpenGL/gl.h'):
        if not conf.CheckLib('opengl32') or not conf.CheckCHeader('GL/gl.h'):
            print "Could not find libGL or could not find 'GL/gl.h'"
            Exit(1)
        else:
            env.Append(LIBS=['opengl32'])
    else:
        env.Append(LIBS=['GL'])
else:
    env.Append(LIBS=['GL'])


env.ParseConfig("sdl-config --cflags")
Export('env')

SConscript("src/SConscript")
SConscript("libgag/SConscript")
