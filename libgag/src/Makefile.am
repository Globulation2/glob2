noinst_LIBRARIES = libgag.a

libgag_a_SOURCES = FileManager.cpp\
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
	GUISelector.cpp\
	GUIAnimation.cpp\
	SDLFont.cpp\
	Sprite.cpp\
	GUIMessageBox.cpp \
	SupportFunctions.cpp \
	StringTable.cpp \
	Toolkit.cpp

EXTRA_DIST = SDLFont.h \
        SDLGraphicContext.h \
		GLGraphicContext.h \
        SDL_ttf.h \
		win32_dirent.cpp \
		win32_dirent.h
			

INCLUDES = @FT2_CFLAGS@ @SDL_CFLAGS@ -I@srcdir@/../include/
