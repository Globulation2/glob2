noinst_LIBRARIES = libgag.a

libgag_a_SOURCES = \
	GraphicContext.cpp\
	Sprite.cpp\
	TrueTypeFont.cpp\
	TrueTypeFont.h\
	FileManager.cpp\
	Toolkit.cpp\
	StringTable.cpp\
	SupportFunctions.cpp\
	SDL_ttf.c\
	SDL_ttf.h\
	glSDL.c \
	GUIBase.cpp\
	GUIList.cpp\
	GUIFileList.cpp\
	GUIRatio.cpp\
	GUITextArea.cpp\
	GUIButton.cpp\
	GUINumber.cpp\
	GUIText.cpp\
	GUITextInput.cpp\
	GUISelector.cpp\
	GUIAnimation.cpp\
	GUIMessageBox.cpp\
	BinaryStream.h\
	TextStream.h
	
EXTRA_DIST = win32_dirent.cpp win32_dirent.h
			

INCLUDES = @FT2_CFLAGS@ @SDL_CFLAGS@ -I@srcdir@/../include/
