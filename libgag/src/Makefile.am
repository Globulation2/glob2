noinst_LIBRARIES = libgag.a

libgag_a_SOURCES = base.cpp\
	baseserial.cpp\
	xmlserializer.cpp\
	FileManager.cpp\
	GUIBase.cpp\
	GUIList.cpp\
	GUIFileList.cpp\
	GUIRatio.cpp\
	GUITextArea.cpp\
	GraphicContext.cpp\
	SDLGraphicContext.cpp\
	GLGraphicContext.cpp \
	SDL_ttf.c\
	GUIButton.cpp\
	GUINumber.cpp\
	GUIText.cpp\
	GUITextInput.cpp\
	SDLFont.cpp\
	SDLSprite.cpp\
	GUIMessageBox.cpp \
	SupportFunctions.cpp \
	Toolkit.cpp

EXTRA_DIST = SDLFont.h \
        SDLGraphicContext.h \
		GLGraphicContext.h \
        SDLSprite.h \
        SDL_ttf.h \
		win32_dirent.cpp \
		win32_dirent.h
			

INCLUDES = @FT2_CFLAGS@ @SDL_CFLAGS@ -I../include/
