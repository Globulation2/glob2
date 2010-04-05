;------------------------------------
;Settings
;------------------------------------
  Name "Globulation 2"
  OutFile "globulation2_win32.exe"
  InstallDir $PROGRAMFILES\Globulation_2
  InstallDirRegKey HKCU "Software\Globulation_2" ""
  Var StartMenuFolder
  Var STARTMENU_FOLDER
  RequestExecutionLevel admin  ;Vista Setting

;------------------------------------
;Style
;------------------------------------
  !include "MUI2.nsh"
  !define MUI_WELCOMEFINISHPAGE_BITMAP "side.bmp"
  !define MUI_UNWELCOMEFINISHPAGE_BITMAP "side.bmp"
  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_BITMAP "header.bmp"
  !define MUI_ABORTWARNING

;------------------------------------
;Store the users Language Selection
;------------------------------------
  !define MUI_LANGDLL_REGISTRY_ROOT "HKCU" 
  !define MUI_LANGDLL_REGISTRY_KEY "Software\Globulation_2" 
  !define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

;------------------------------------
;Pages
;------------------------------------
  ;------------------------------------
  ;Install Pages
  ;------------------------------------
    !insertmacro MUI_PAGE_WELCOME
    !insertmacro MUI_PAGE_LICENSE "..\COPYING"
    !insertmacro MUI_PAGE_DIRECTORY
    !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU" 
    !define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Globulation_2" 
    !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
    !insertmacro MUI_PAGE_STARTMENU Application $STARTMENU_FOLDER
    !insertmacro MUI_PAGE_INSTFILES
    !define MUI_FINISHPAGE_RUN $INSTDIR\glob2.exe
    !insertmacro MUI_PAGE_FINISH
  ;------------------------------------
  ;Uninstall Pages
  ;------------------------------------
    !insertmacro MUI_UNPAGE_CONFIRM
    !insertmacro MUI_UNPAGE_INSTFILES
    !insertmacro MUI_UNPAGE_FINISH

;------------------------------------
;Language Selection Dialog Choices
;------------------------------------
  !insertmacro MUI_LANGUAGE "English"
  !insertmacro MUI_LANGUAGE "French"
  !insertmacro MUI_LANGUAGE "German"
  !insertmacro MUI_LANGUAGE "Spanish"
  !insertmacro MUI_LANGUAGE "SpanishInternational"
  !insertmacro MUI_LANGUAGE "Italian"
  !insertmacro MUI_LANGUAGE "Dutch"
  !insertmacro MUI_LANGUAGE "Danish"
  !insertmacro MUI_LANGUAGE "Swedish"
  !insertmacro MUI_LANGUAGE "Russian"
  !insertmacro MUI_LANGUAGE "Portuguese"
  !insertmacro MUI_LANGUAGE "PortugueseBR"
;------------------------------------
;Commands for the installer
;------------------------------------
  Section ""
  SectionIn RO
  !include "install_list.nsh" ;List of files to install (generated from windows/SConscript)
  File ..\data\authors.txt
  File ..\COPYING
  File ..\src\glob2.exe
  File glob2.ico
  ;------------------------------------
  ;Install shortcuts
  ;------------------------------------
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$STARTMENU_FOLDER"
    CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk" "$INSTDIR\glob2win32-uninst.exe" "" "$INSTDIR\glob2win32-uninst.exe" 0
    CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Globulation 2.lnk" "$INSTDIR\glob2.exe" "" "$INSTDIR\glob2.ico"
    CreateShortCut "$DESKTOP\Globulation 2.lnk" "$INSTDIR\glob2.exe" "" "$INSTDIR\glob2.ico"
    !insertmacro MUI_STARTMENU_WRITE_END
  ;------------------------------------
  ;Set registery values
  ;------------------------------------
    WriteRegStr HKCU "Software\Globulation_2" "" $INSTDIR
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "DisplayName" "Globulation 2"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "UninstallString" "$INSTDIR\glob2win32-uninst.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "DisplayIcon" "$INSTDIR\glob2.ico"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "NoModify" "1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "NoRepair" "1"

  WriteUninstaller "glob2win32-uninst.exe"
  SectionEnd

;------------------------------------
;Commands for the uninstaller
;------------------------------------
  Section "Uninstall"
  !include "uninstall_list.nsh" ;List of files to uninstall (generated from windows/SConscript)
  Delete $INSTDIR\authors.txt
  Delete $INSTDIR\COPYING
  Delete $INSTDIR\glob2.exe
  Delete $INSTDIR\glob2.ico
  Delete $INSTDIR\glob2win32-uninst.exe
  Delete $INSTDIR\keyboard-gui.txt
  Delete $INSTDIR\keyboard-mapedit.txt
  Delete $INSTDIR\preferences.txt
  Delete $INSTDIR\stderr.txt
  Delete $INSTDIR\stdout.txt
  Delete $INSTDIR\games\*.game
  RMDir $INSTDIR\games
  RMDir $INSTDIR\logs
  RMDir $INSTDIR\videoshots
  RMDir $INSTDIR
  
  ;------------------------------------
  ;Delete shortcuts
  ;------------------------------------
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Globulation 2.lnk"
	RMDir "$SMPROGRAMS\$StartMenuFolder"
    Delete "$DESKTOP\Globulation 2.lnk"
  ;------------------------------------
  ;Delete registery keys
  ;------------------------------------
    DeleteRegKey /ifempty HKCU "Software\Globulation_2"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2"

  SectionEnd

;------------------------------------
;Installer Functions
;------------------------------------
  Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
  FunctionEnd
  
;------------------------------------
;Uninstaller Functions
;------------------------------------
  Function un.onInit
  ;------------------------------------
  ;Make sure Globulation 2 isn't running
  ;------------------------------------  
    FindWindow $0 "" "Globulation 2"
    StrCmp $0 0 continueInstall
    MessageBox MB_ICONSTOP|MB_OK "The application you are trying to remove is currently running. Close it and try again."
    Abort
    continueInstall:
  !insertmacro MUI_UNGETLANGUAGE
  FunctionEnd