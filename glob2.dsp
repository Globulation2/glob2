# Microsoft Developer Studio Project File - Name="glob2" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=glob2 - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "glob2.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "glob2.mak" CFG="glob2 - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "glob2 - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "glob2 - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "glob2 - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GR /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x100c /d "NDEBUG"
# ADD RSC /l 0x100c /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib SDL.lib SDL_image.lib SDLmain.lib SDL_net.lib /nologo /subsystem:windows /machine:I386

!ELSEIF  "$(CFG)" == "glob2 - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GR /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x100c /d "_DEBUG"
# ADD RSC /l 0x100c /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib SDL.lib SDL_image.lib SDLmain.lib SDL_net.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "glob2 - Win32 Release"
# Name "glob2 - Win32 Debug"
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=.\src\AI.cpp
# End Source File
# Begin Source File

SOURCE=.\src\AI.h
# End Source File
# Begin Source File

SOURCE=.\src\Building.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Building.h
# End Source File
# Begin Source File

SOURCE=.\src\BuildingType.cpp
# End Source File
# Begin Source File

SOURCE=.\src\BuildingType.h
# End Source File
# Begin Source File

SOURCE=.\src\CustomGameScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\CustomGameScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Engine.h
# End Source File
# Begin Source File

SOURCE=.\src\EntityType.cpp
# End Source File
# Begin Source File

SOURCE=.\src\EntityType.h
# End Source File
# Begin Source File

SOURCE=.\src\Fatal.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Fatal.h
# End Source File
# Begin Source File

SOURCE=.\src\FileManager.cpp
# End Source File
# Begin Source File

SOURCE=.\src\FileManager.h
# End Source File
# Begin Source File

SOURCE=.\src\GAG.h
# End Source File
# Begin Source File

SOURCE=.\src\Game.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Game.h
# End Source File
# Begin Source File

SOURCE=.\src\GameGUI.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GameGUI.h
# End Source File
# Begin Source File

SOURCE=.\src\GameGUIDialog.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GameGUIDialog.h
# End Source File
# Begin Source File

SOURCE=.\src\GameGUILoadSave.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GameGUILoadSave.h
# End Source File
# Begin Source File

SOURCE=.\src\Glob2.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Glob2.h
# End Source File
# Begin Source File

SOURCE=.\src\GlobalContainer.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GlobalContainer.h
# End Source File
# Begin Source File

SOURCE=.\src\GraphicContext.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GraphicContext.h
# End Source File
# Begin Source File

SOURCE=.\src\GUIBase.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUIBase.h
# End Source File
# Begin Source File

SOURCE=.\src\GUIButton.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUIButton.h
# End Source File
# Begin Source File

SOURCE=.\src\GUIList.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUIList.h
# End Source File
# Begin Source File

SOURCE=.\src\GUIMapPreview.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUIMapPreview.h
# End Source File
# Begin Source File

SOURCE=.\src\GUIText.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUIText.h
# End Source File
# Begin Source File

SOURCE=.\src\GUITextArea.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUITextArea.h
# End Source File
# Begin Source File

SOURCE=.\src\GUITextInput.cpp
# End Source File
# Begin Source File

SOURCE=.\src\GUITextInput.h
# End Source File
# Begin Source File

SOURCE=.\src\Header.h
# End Source File
# Begin Source File

SOURCE=.\src\Map.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Map.h
# End Source File
# Begin Source File

SOURCE=.\src\MapEdit.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MapEdit.h
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersConnectedScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersConnectedScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersHost.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersHost.h
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersHostScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersHostScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersJoin.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersJoin.h
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersJoinScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\MultiplayersJoinScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\NetConsts.h
# End Source File
# Begin Source File

SOURCE=.\src\NetGame.cpp
# End Source File
# Begin Source File

SOURCE=.\src\NetGame.h
# End Source File
# Begin Source File

SOURCE=.\src\NewMapScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\NewMapScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\NonANSICStdWrapper.h
# End Source File
# Begin Source File

SOURCE=.\src\Order.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Order.h
# End Source File
# Begin Source File

SOURCE=.\src\Player.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Player.h
# End Source File
# Begin Source File

SOURCE=.\src\PreparationGui.cpp
# End Source File
# Begin Source File

SOURCE=.\src\PreparationGui.h
# End Source File
# Begin Source File

SOURCE=.\src\Race.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Race.h
# End Source File
# Begin Source File

SOURCE=.\src\SDLFont.cpp
# End Source File
# Begin Source File

SOURCE=.\src\SDLFont.h
# End Source File
# Begin Source File

SOURCE=.\src\SDLGraphicContext.cpp
# End Source File
# Begin Source File

SOURCE=.\src\SDLGraphicContext.h
# End Source File
# Begin Source File

SOURCE=.\src\SDLSprite.cpp
# End Source File
# Begin Source File

SOURCE=.\src\SDLSprite.h
# End Source File
# Begin Source File

SOURCE=.\src\Session.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Session.h
# End Source File
# Begin Source File

SOURCE=.\src\SettingsScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\SettingsScreen.h
# End Source File
# Begin Source File

SOURCE=.\src\StringTable.cpp
# End Source File
# Begin Source File

SOURCE=.\src\StringTable.h
# End Source File
# Begin Source File

SOURCE=.\src\Team.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Team.h
# End Source File
# Begin Source File

SOURCE=.\src\TeamStat.cpp
# End Source File
# Begin Source File

SOURCE=.\src\TeamStat.h
# End Source File
# Begin Source File

SOURCE=.\src\Unit.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Unit.h
# End Source File
# Begin Source File

SOURCE=.\src\UnitType.cpp
# End Source File
# Begin Source File

SOURCE=.\src\UnitType.h
# End Source File
# Begin Source File

SOURCE=.\src\Utilities.cpp
# End Source File
# Begin Source File

SOURCE=.\src\Utilities.h
# End Source File
# Begin Source File

SOURCE=.\src\YOGConnector.cpp
# End Source File
# Begin Source File

SOURCE=.\src\YOGConnector.h
# End Source File
# Begin Source File

SOURCE=.\src\YOGScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\YOGScreen.h
# End Source File
# End Target
# End Project
