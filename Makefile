SRCDIR=src
GLOB2FILES=GraphicContext.cpp Sprite.cpp Glob2.cpp NetGame.cpp Order.cpp EntityType.cpp Player.cpp Unit.cpp Building.cpp Team.cpp Map.cpp BuildingType.cpp Race.cpp PreparationGui.cpp MapEdit.cpp GUIBase.cpp GUIButton.cpp Game.cpp Session.cpp GlobalContainer.cpp Utilities.cpp UnitType.cpp Fatal.cpp Engine.cpp GameGUI.cpp AI.cpp StringTable.cpp GUITextInput.cpp GUIList.cpp GameGUIDialog.cpp
GLOB2OBJ=$(patsubst %.cpp,%.o,$(GLOB2FILES))
GLOB2SRC=$(patsubst %,$(SRCDIR)/%,$(GLOB2FILES))

CXX=g++
GLOB2EXE=glob2
CXXFLAGS=-g -Wall -ansi `sdl-config --cflags`

VPATH=$(SRCDIR)

$(GLOB2EXE): $(GLOB2OBJ) 
	$(CXX) -g `sdl-config --libs` -lSDL_image -lSDL_net -ljpeg -o $(GLOB2EXE) $+

stable: $(GLOB2OBJ) $(GLOB2EXE)
	rm -rf glob2stable
	cp $(GLOB2EXE) glob2stable

depend: Makefile
	$(CXX) -MM $(CXXFLAGS) $(GLOB2SRC) > $@

clean:
	rm -f $(GLOB2OBJ) $(GLOB2EXE) depend

superclean:
	rm -f $(GLOB2OBJ) $(GLOB2EXE) depend
	rm -f glob*stable*

include depend

