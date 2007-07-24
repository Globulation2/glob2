;------------------------------------
;Settings
;------------------------------------
  Name "Globulation 2"
  OutFile "glob2win32_alpha24.exe"
  InstallDir $PROGRAMFILES\Globulation_2
  ;------------------------------------
  ;Setup registery key
  ;------------------------------------
    InstallDirRegKey HKCU "Software\Globulation_2" ""
  ;------------------------------------
  ;Variables
  ;------------------------------------
    Var MUI_TEMP
    Var STARTMENU_FOLDER


;------------------------------------
;Style
;------------------------------------
  !include "MUI.nsh"
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
  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE "..\COPYING"
  !insertmacro MUI_PAGE_DIRECTORY
  ;------------------------------------
  ;Store the users start menu name choice
  ;------------------------------------
    !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU" 
    !define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Globulation_2" 
    !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
  !insertmacro MUI_PAGE_STARTMENU Application $STARTMENU_FOLDER
  !insertmacro MUI_PAGE_INSTFILES
  ;------------------------------------
  ;Add checkbox to run game on exit
  ;------------------------------------  
    !define MUI_FINISHPAGE_RUN $INSTDIR\glob2.exe
  !insertmacro MUI_PAGE_FINISH
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  !insertmacro MUI_UNPAGE_FINISH


;------------------------------------
;Language Selection Dialog Choices
;------------------------------------
  !insertmacro MUI_LANGUAGE "English"


;------------------------------------
;Commands for the installer
;------------------------------------
  Section ""
  SectionIn RO
  ;------------------------------------
  ;Install Globulation 2 runtime
  ;------------------------------------
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
      File ..\src\glob2.exe
      File ..\*.dll
      File glob2.ico
      File ..\AUTHORS
      File ..\COPYING
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
  ;------------------------------------
  ;Create uninstaller file
  ;------------------------------------
    WriteUninstaller "glob2win32-uninst.exe"
  SectionEnd


;------------------------------------
;Installer Functions
;------------------------------------
  Function .onInit
  ;------------------------------------
  ;Show already running installer
  ;------------------------------------  
    System::Call "kernel32::CreateMutexA(i 0, i 0, t '$(^Name)') i .r0 ?e"
    Pop $0
    StrCmp $0 0 launch
    StrLen $0 "$(^Name)"
    IntOp $0 $0 + 1
    loop:
    FindWindow $1 '#32770' '' 0 $1
    IntCmp $1 0 +4
    System::Call "user32::GetWindowText(i r1, t .r2, i r0) i."
    StrCmp $2 "$(^Name)" 0 loop
    System::Call "user32::SetForegroundWindow(i r1) i."
    Abort
    launch:
  !insertmacro MUI_LANGDLL_DISPLAY
  FunctionEnd


;------------------------------------
;Commands for the uninstaller
;------------------------------------
  Section "Uninstall"
  ;------------------------------------
  ;Delete Globulation 2 files and directories
  ;------------------------------------
    RMDir /r "$INSTDIR"
  ;------------------------------------
  ;Delete shortcuts
  ;------------------------------------
    !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
    Delete "$SMPROGRAMS\$MUI_TEMP\*.*"
    Delete "$DESKTOP\Globulation 2.lnk"
    ;------------------------------------
    ;Delete empty start menu directories
    ;------------------------------------
      StrCpy $MUI_TEMP "$SMPROGRAMS\$MUI_TEMP"
      startMenuDeleteLoop:
      ClearErrors
      RMDir $MUI_TEMP
      GetFullPathName $MUI_TEMP "$MUI_TEMP\.."
      IfErrors startMenuDeleteLoopDone
      StrCmp $MUI_TEMP $SMPROGRAMS startMenuDeleteLoopDone startMenuDeleteLoop
      startMenuDeleteLoopDone:
  ;------------------------------------
  ;Delete registery keys
  ;------------------------------------
    DeleteRegKey /ifempty HKCU "Software\Globulation_2"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2"
  SectionEnd


;------------------------------------
;Uninstaller Functions
;------------------------------------
  Function un.onInit
  ;------------------------------------
  ;Show already running uninstaller
  ;------------------------------------  
    System::Call "kernel32::CreateMutexA(i 0, i 0, t '$(^Name)') i .r0 ?e"
    Pop $0
    StrCmp $0 0 launch
    StrLen $0 "$(^Name)"
    IntOp $0 $0 + 1
    loop:
    FindWindow $1 '#32770' '' 0 $1
    IntCmp $1 0 +4
    System::Call "user32::GetWindowText(i r1, t .r2, i r0) i."
    StrCmp $2 "$(^Name)" 0 loop
    System::Call "user32::SetForegroundWindow(i r1) i."
    Abort
    launch:
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