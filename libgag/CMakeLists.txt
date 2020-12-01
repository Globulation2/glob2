cmake_minimum_required(VERSION 3.13)

include_directories(include)

set(GAG_SOURCES
    src/BinaryStream.cpp
    src/FileManager.cpp
    src/FormatableString.cpp
    src/Stream.cpp
    src/StreamBackend.cpp
    src/StreamFilter.cpp
    src/StringTable.cpp
    src/TextStream.cpp
    src/Toolkit.cpp
    )
if (SERVER_ONLY)
else()
    set(GAG_SOURCES 
        ${GAG_SOURCES}
        src/CursorManager.cpp
        src/GUIAnimation.cpp
        src/GUIBase.cpp
        src/GUIButton.cpp
        src/GUICheckList.cpp
        src/GUIFileList.cpp
        src/GUIImage.cpp
        src/GUIKeySelector.cpp
        src/GUIList.cpp
        src/GUIMessageBox.cpp
        src/GUINumber.cpp
        src/GUIProgressBar.cpp
        src/GUIRatio.cpp
        src/GUISelector.cpp
        src/GUIStyle.cpp
        src/GUITabScreen.cpp
        src/GUITabScreenWindow.cpp
        src/GUIText.cpp
        src/GUITextArea.cpp
        src/GUITextInput.cpp
        src/GraphicContext.cpp
        src/KeyPress.cpp
        src/Sprite.cpp
        src/SupportFunctions.cpp
        src/TextSort.cpp
        src/TrueTypeFont.cpp
        src/win32_dirent.cpp
        )
endif()
add_library(gag ${GAG_SOURCES})

target_link_libraries(gag SDL2_ttf)
