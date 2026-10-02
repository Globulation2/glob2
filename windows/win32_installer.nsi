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
  Push $3
  ClearErrors
  FileOpen $0 "$INSTDIR\.glob2-owned-files.txt" r
  IfErrors 0 opened
  ; Absence is expected during a first legacy upgrade; unreadable metadata is
  ; different. The uninstall section independently requires the inventory.
  IfFileExists "$INSTDIR\.glob2-owned-files.txt" 0 done
    StrCpy $CleanupFailed 1
    Goto done
  opened:
    FileSeek $0 0 END $3
    StrCmp $3 0 invalid
    IntOp $2 $3 % 2
    StrCmp $2 0 +2
    Goto invalid
    FileSeek $0 0 SET
    FileReadUTF16LE $0 $1
    ${${TRIM}} $1 $1
    StrCmp $1 ":GLOB2-OWNED-BEGIN:v1" validate invalid
  validate:
    ClearErrors
    FileReadUTF16LE $0 $1
    IfErrors invalid
    ${${TRIM}} $1 $1
    StrCmp $1 ":GLOB2-OWNED-END:v1" validated
    StrCmp $1 "" validate
    StrCpy $2 $1 1
    StrCmp $2 "\" invalid
    StrCmp $2 "/" invalid
    ${${SEARCH}} $2 $1 ".."
    StrCmp $2 "" +2
    Goto invalid
    ${${SEARCH}} $2 $1 ":"
    StrCmp $2 "" +2
    Goto invalid
    ${${SEARCH}} $2 $1 "*"
    StrCmp $2 "" +2
    Goto invalid
    ${${SEARCH}} $2 $1 "?"
    StrCmp $2 "" +2
    Goto invalid
    Goto validate
  validated:
    ; The terminal marker detects truncation even at a complete UTF-16 record.
    ; No data may follow it, including a second inventory or extra file records.
    ClearErrors
    FileReadUTF16LE $0 $1
    IfErrors +2
    Goto invalid
    FileSeek $0 0 CUR $2
    StrCmp $2 $3 +2
    Goto invalid
    ; Validate the whole inventory before removing anything. An invalid path or
    ; truncated UTF-16 record must leave a repairable installation intact.
    FileSeek $0 0 SET
  remove:
    ClearErrors
    FileReadUTF16LE $0 $1
    IfErrors finished
    ${${TRIM}} $1 $1
    StrCmp $1 "" remove
    StrCmp $1 ":GLOB2-OWNED-BEGIN:v1" remove
    StrCmp $1 ":GLOB2-OWNED-END:v1" finished
    ClearErrors
    Delete "$INSTDIR\$1"
    IfErrors 0 +2
      StrCpy $CleanupFailed 1
    Goto remove
  finished:
    FileSeek $0 0 CUR $2
    StrCmp $2 $3 closed
  invalid:
    StrCpy $CleanupFailed 1
  closed:
    FileClose $0
  done:
  Pop $3
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
    Abort "Previous files or inventory could not be read/removed. Close Globulation 2, check the installation and retry."
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
  ; Invite links: glob2://join?instance=...&code=...
  WriteRegStr HKCR "glob2" "" "URL:Globulation 2 invite"
  WriteRegStr HKCR "glob2" "URL Protocol" ""
  WriteRegStr HKCR "glob2\DefaultIcon" "" "$INSTDIR\glob2.exe,0"
  WriteRegStr HKCR "glob2\shell\open\command" "" '"$INSTDIR\glob2.exe" "%1"'
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
  IfFileExists "$INSTDIR\.glob2-owned-files.txt" inventoryPresent
    SetErrorLevel 1
    Abort "The installation inventory is missing. Reinstall Globulation 2 before uninstalling."
  inventoryPresent:
  Call un.ClearOwned
  ${If} $CleanupFailed != 0
    SetErrorLevel 1
    Abort "Application files or inventory could not be read/removed. Close Globulation 2 and retry; recovery metadata has been retained."
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
  DeleteRegKey HKCR "glob2"
SectionEnd
