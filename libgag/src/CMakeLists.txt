set(libgag_SOURCES
BinaryStream.cpp
CursorManager.cpp
FileManager.cpp
FormatableString.cpp
GraphicContext.cpp
GUIAnimation.cpp
GUIBase.cpp
GUIButton.cpp
GUIKeySelector.cpp
GUIFileList.cpp
GUIList.cpp
GUIMessageBox.cpp
GUINumber.cpp
GUIRatio.cpp
GUISelector.cpp
GUIStyle.cpp
GUITextArea.cpp
GUIText.cpp
GUITextInput.cpp
Sprite.cpp
StreamBackend.cpp
Stream.cpp
StreamFilter.cpp
StringTable.cpp
SupportFunctions.cpp
TextStream.cpp
Toolkit.cpp
TrueTypeFont.cpp
win32_dirent.cpp
)

#TODO: remove this from the code
add_definitions(-DHAVE_OPENGL)

include_directories(${libgag_SOURCE_DIR}/include ${SDL_INCLUDE_DIR})

add_library(gag ${libgag_SOURCES})
