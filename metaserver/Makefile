CFLAGS = -g -Wall `sdl-config --cflags`
CXXFLAGS = -g -Wall `sdl-config --cflags`
LINKFLAGS = -g `sdl-config --libs` -lSDL_net
C = gcc
XX = g++
LINKER = g++
#CC = gcc-3.0
#CXX = g++-3.0
#LINKER = g++-3.0

all: YOGServer

YOGClient.o: YOGClient.cpp
	$(CXX) $(CXXFLAGS) -c YOGClient.cpp -o YOGClient.o

YOGServer.o: YOGServer.cpp
	$(CXX) $(CXXFLAGS) -c YOGServer.cpp -o YOGServer.o

YOGServer: YOGClient.o YOGServer.o ../src/Utilities.o
	$(LINKER) $(LINKFLAGS) YOGServer.o YOGClient.o ../src/Utilities.o -o YOGServer
	
clean:
	rm -f core YOGServer.o YOGClient.o YOGServer
