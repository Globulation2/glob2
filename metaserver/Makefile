all: YOGServer
	
YOGClient.o: YOGClient.cpp
	c++ `sdl-config --cflags` -g -Wall -c YOGClient.cpp -o YOGClient.o

YOGServer.o: YOGServer.cpp
	c++ `sdl-config --cflags` -g -Wall -c YOGServer.cpp -o YOGServer.o

YOGServer: YOGClient.o YOGServer.o ../src/Utilities.o
	c++ -g `sdl-config --libs` -lSDL_net YOGServer.o YOGClient.o ../src/Utilities.o -o YOGServer
	
clean:
	rm -f core YOGServer.o YOGClient.o YOGServer
