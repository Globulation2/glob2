all: broad serv

broad: broadTest.cpp
	gcc -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` broadTest.cpp -o broadTest
	
serv: simpleServ.cpp
	gcc -g `sdl-config --libs` -lSDL_net `sdl-config --cflags` simpleServ.cpp -o simpleServ
	
clean:
	rm -f broadTest simpleServ core
