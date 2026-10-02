; Built only through tools/release/package_nsis.py from the shared staged runtime.
Unicode true
Name "Globulation 2"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\Globulation_2"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!include "StrFunc.nsh"
${StrStr}
${UnStrStr}
Var StartMenuFolder
Var CleanupFailed
!define MUI_ABORTWARNING
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Globulation_2"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\COPYING"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\glob2.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
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

; Never recursively remove an installation: saved games and unrelated files may
; be present. The inventory lists only files installed by this package. Validate
; each relative path before accepting an inventory from a previous installation.
!macro OwnedFunction PREFIX SEARCH TRIM
Function ${PREFIX}ClearOwned
  StrCpy $CleanupFailed 0
  Push $0
  Push $1
  Push $2
  ClearErrors
  FileOpen $0 "$INSTDIR\.glob2-owned-files.txt" r
  IfErrors done
  loop:
    ClearErrors
    FileReadUTF16LE $0 $1
    IfErrors closed
    ${${TRIM}} $1 $1
    StrCmp $1 "" loop
    StrCpy $2 $1 1
    StrCmp $2 "\" loop
    StrCmp $2 "/" loop
    ${${SEARCH}} $2 $1 ".."
    StrCmp $2 "" +2
    Goto loop
    ${${SEARCH}} $2 $1 ":"
    StrCmp $2 "" +2
    Goto loop
    ClearErrors
    Delete "$INSTDIR\$1"
    IfErrors 0 +2
      StrCpy $CleanupFailed 1
    Goto loop
  closed:
    FileClose $0
  done:
  Pop $2
  Pop $1
  Pop $0
FunctionEnd
!macroend
!insertmacro OwnedFunction "" StrStr TrimNewLines
!insertmacro OwnedFunction "un." UnStrStr un.TrimNewLines

Function .onInit
  ${IfNot} ${RunningX64}
    SetErrorLevel 1
    Abort "This package requires 64-bit Windows."
  ${EndIf}
  SetRegView 64
  ; Keep the existing installation location, including the legacy 32-bit key.
  ReadRegStr $0 HKCU "Software\Globulation_2" ""
  ${If} $0 == ""
    SetRegView 32
    ReadRegStr $0 HKCU "Software\Globulation_2" ""
    SetRegView 64
  ${EndIf}
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Section "Install"
  SetShellVarContext all
  Call ClearOwned
  ${If} $CleanupFailed != 0
    SetErrorLevel 1
    Abort "Close Globulation 2 and retry. Previous application files could not be removed."
  ${EndIf}
  ClearErrors
  !include "${LIST_DIR}\install.nsh"
  IfErrors 0 +3
    SetErrorLevel 1
    Abort "Could not install application files. Close Globulation 2 and retry."
  SetOutPath "$INSTDIR"
  File /oname=.glob2-owned-files.txt "${LIST_DIR}\owned.txt"
  WriteUninstaller "$INSTDIR\glob2win32-uninst.exe"
  WriteRegStr HKCU "Software\Globulation_2" "" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "DisplayName" "Globulation 2"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "UninstallString" '"$INSTDIR\glob2win32-uninst.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2" "NoRepair" 1
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Globulation 2.lnk" "$INSTDIR\glob2.exe"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\glob2win32-uninst.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
  CreateShortCut "$DESKTOP\Globulation 2.lnk" "$INSTDIR\glob2.exe"
SectionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  Call un.ClearOwned
  ${If} $CleanupFailed != 0
    SetErrorLevel 1
    Abort "Close Globulation 2 and retry uninstall. Recovery metadata has been retained."
  ${EndIf}
  Delete "$INSTDIR\.glob2-owned-files.txt"
  Delete "$INSTDIR\glob2win32-uninst.exe"
  !include "${LIST_DIR}\directories.nsh"
  RMDir "$INSTDIR"
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  Delete "$SMPROGRAMS\$StartMenuFolder\Globulation 2.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  Delete "$DESKTOP\Globulation 2.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Globulation_2"
  DeleteRegKey HKCU "Software\Globulation_2"
SectionEnd
