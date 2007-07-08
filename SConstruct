env = Environment()
conf = Configure(env)
env.Append(CPPPATH='#libgag/include')
env.Append(LIBPATH='#libgag/src')
env.Append(LIBS=['SDL', 'SDL_ttf', 'SDL_image', 'SDL_net', 'GL', 'GLU', 'speex', 'vorbisfile'])
env.ParseConfig("sdl-config --cflags")
Export('env')


SConscript("src/SConscript")
SConscript("libgag/SConscript")
