all: broadTest simpleServer simpleClient

broadTest: broadTest.cpp
	gcc -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` broadTest.cpp -o broadTest
	
simpleServer: simpleServer.cpp
	gcc -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` simpleServer.cpp -o simpleServer
	
simpleClient: simpleClient.cpp
	gcc -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` simpleClient.cpp -o simpleClient
	
clean:
	rm -f broadTest simpleServer simpleClient core
