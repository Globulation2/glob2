noinst_LIBRARIES = libgag.a

libgag_a_EXTRA_SRC = \
	win32_dirent.h\
	win32_dirent.cpp

libgag_a_SOURCES = \
	BinaryStream.cpp\
	CursorManager.cpp\
	FileManager.cpp\
	FormatableString.cpp\
	GraphicContext.cpp\
	GUIAnimation.cpp\
	GUIBase.cpp\
	GUIButton.cpp\
	GUIFileList.cpp\
	GUIKeySelector.cpp\
	GUIList.cpp\
	GUIMessageBox.cpp\
	GUINumber.cpp\
	GUIRatio.cpp\
	GUISelector.cpp\
	GUIStyle.cpp\
	GUITextArea.cpp\
	GUIText.cpp\
	GUITextInput.cpp\
	KeyPress.cpp\
	Sprite.cpp\
	StreamBackend.cpp\
	Stream.cpp\
	StreamFilter.cpp\
	StringTable.cpp\
	SupportFunctions.cpp\
	TextStream.cpp\
	Toolkit.cpp\
	TrueTypeFont.cpp\
	TrueTypeFont.h\
	$(libgag_a_EXTRA_SRC)

INCLUDES = @SDL_CFLAGS@ -I@srcdir@/../include/
