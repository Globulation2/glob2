all: broadTest simpleServer simpleClient chaterYog

broadTest: broadTest.cpp
	g++ -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` broadTest.cpp -o broadTest
	
simpleServer: simpleServer.cpp
	g++ -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` simpleServer.cpp -o simpleServer
	
simpleClient: simpleClient.cpp
	g++ -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` simpleClient.cpp -o simpleClient
	
chaterYog: chaterYog.cpp
	g++ -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` chaterYog.cpp -o chaterYog

clean:
	rm -f broadTest simpleServer simpleClient chaterYog core
