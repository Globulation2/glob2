noinst_LIBRARIES = libgag.a

libgag_a_SOURCES = FileManager.cpp\
	GUIBase.cpp\
	GUIList.cpp\
	GUIRatio.cpp\
	GUITextArea.cpp\
	GraphicContext.cpp\
	SDLGraphicContext.cpp\
	GLGraphicContext.cpp \
	SDL_ttf.c\
	GAG.cpp\
	GUIButton.cpp\
	GUINumber.cpp\
	GUIText.cpp\
	GUITextInput.cpp\
	SDLFont.cpp\
	SDLSprite.cpp\
	GUIMessageBox.cpp \
	SupportFunctions.cpp

EXTRA_DIST = SDLFont.h \
        SDLGraphicContext.h \
		GLGraphicConext.cpp \
        SDLSprite.h \
        SDL_ttf.h
			

INCLUDES = @FT2_CFLAGS@ @SDL_CFLAGS@ -I../include/
