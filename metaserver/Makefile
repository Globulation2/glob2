all: YOGServer

YOGServer.o: YOGServer.cpp
	c++ `sdl-config --cflags` -g -Wall -c YOGServer.cpp -o YOGServer.o
	
YOGClient.o: YOGClient.cpp
	c++ `sdl-config --cflags` -g -Wall -c YOGClient.cpp -o YOGClient.o

YOGServer: YOGServer.o YOGClient.o
	c++ -g `sdl-config --libs` -lSDL_net YOGServer.o YOGClient.o -o YOGServer
	
clean:
	rm -f core YOGServer.o YOGClient.o YOGServer