!include "MUI.nsh"

Name "Globulation 2"
OutFile "Globulation2-0.8.24.exe"
InstallDir "$PROGRAMFILES\Globulation 2"
InstallDirRegKey HKU "Software\Globulation 2" ""

var STARTMENU_FOLDER
var MUI_TEMP

!define MUI_PAGE_HEADER_TEXT "Installing Globulation 2"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\COPYING"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKU" 
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Globulation 2" 
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!insertmacro MUI_PAGE_STARTMENU Application $STARTMENU_FOLDER
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Globulation 2"
  SetOutPath $INSTDIR
  File ..\src\glob2.exe
  File ..\src\*.dll
  SetOutPath $INSTDIR\data
  File /r ..\data\*
  SetOutPath $INSTDIR\campaigns
  File /nonfatal ..\campaigns\*.txt
  File /nonfatal ..\campaigns\*.map
  SetOutPath $INSTDIR\maps
  File /nonfatal ..\maps\*.map
  SetOutPath $INSTDIR\scripts
  File /nonfatal ..\scripts\*.sgsl

  SetOutPath $INSTDIR
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  File glob2-icon.ico

  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$STARTMENU_FOLDER"
    CreateShortcut "$SMPROGRAMS\$STARTMENU_FOLDER\Globulation 2.lnk" "$INSTDIR\glob2.exe" "" "$INSTDIR\glob2-icon.ico" 0
    CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END

SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
  Delete "$SMPROGRAMS\$MUI_TEMP\Uninstall.lnk"
  Delete "$SMPROGRAMS\$MUI_TEMP\Globulation 2.lnk"

  ;Delete empty start menu parent diretories
  StrCpy $MUI_TEMP "$SMPROGRAMS\$MUI_TEMP"
 
  startMenuDeleteLoop:
	ClearErrors
    RMDir $MUI_TEMP
    GetFullPathName $MUI_TEMP "$MUI_TEMP\.."
    
    IfErrors startMenuDeleteLoopDone
  
    StrCmp $MUI_TEMP $SMPROGRAMS startMenuDeleteLoopDone startMenuDeleteLoop
  startMenuDeleteLoopDone:

  DeleteRegKey /ifempty HKU "Software\Globulation 2"
SectionEnd