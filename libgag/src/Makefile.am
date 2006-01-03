noinst_LIBRARIES = libgag.a

if MINGW
libgag_a_EXTRA_SRC = \
	win32_dirent.h\
	win32_dirent.cpp
	
else
libgag_a_EXTRA_SRC = 
endif

libgag_a_SOURCES = \
	GraphicContext.cpp\
	Sprite.cpp\
	TrueTypeFont.cpp\
	TrueTypeFont.h\
	FileManager.cpp\
	Toolkit.cpp\
	StringTable.cpp\
	CursorManager.cpp\
	SupportFunctions.cpp\
	SDL_ttf.c\
	SDL_ttf.h\
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
	GUIStyle.cpp\
	StreamBackend.cpp\
	StreamFilter.cpp\
	Stream.cpp\
	BinaryStream.cpp\
	TextStream.cpp\
	$(libgag_a_EXTRA_SRC)

INCLUDES = @FT2_CFLAGS@ @SDL_CFLAGS@ -I@srcdir@/../include/
