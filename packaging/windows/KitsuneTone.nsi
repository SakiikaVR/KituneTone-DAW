Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef APPDIR
  !error "APPDIR must point to the prepared KitsuneTone directory"
!endif
!ifndef OUTFILE
  !define OUTFILE "KitsuneTone-2.2.2-win64-setup.exe"
!endif

Name "狐Tone (KitsuneTone) 2.2.2"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\KitsuneTone"
InstallDirRegKey HKLM "Software\KitsuneTone" "InstallDir"
RequestExecutionLevel admin

VIProductVersion "2.2.2.0"
VIAddVersionKey /LANG=1041 "ProductName" "KitsuneTone"
VIAddVersionKey /LANG=1041 "ProductVersion" "2.2.2"
VIAddVersionKey /LANG=1041 "FileVersion" "2.2.2"
VIAddVersionKey /LANG=1041 "FileDescription" "狐Tone インストーラ"
VIAddVersionKey /LANG=1041 "CompanyName" "KitsuneTone contributors"
VIAddVersionKey /LANG=1041 "LegalCopyright" "LMMS contributors; KitsuneTone contributors"

!define MUI_ICON "${__FILEDIR__}\..\..\cmake\nsis\icon.ico"
!define MUI_UNICON "${__FILEDIR__}\..\..\cmake\nsis\icon.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\..\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "English"

Section "狐Tone" SecMain
  SetOutPath "$INSTDIR"
  File /r /x "vc_redist.x64.exe" "${APPDIR}\*"

  InitPluginsDir
  File /oname=$PLUGINSDIR\vc_redist.x64.exe "${APPDIR}\vc_redist.x64.exe"
  ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart'

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\KitsuneTone" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "DisplayName" "狐Tone (KitsuneTone)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "DisplayIcon" "$INSTDIR\KitsuneTone.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "DisplayVersion" "2.2.2"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\KitsuneTone"
  CreateShortcut "$SMPROGRAMS\KitsuneTone\狐Tone.lnk" "$INSTDIR\KitsuneTone.exe"
  CreateShortcut "$SMPROGRAMS\KitsuneTone\アンインストール.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCR ".ktp" "" "KitsuneTone.Project"
  WriteRegStr HKCR ".ktpz" "" "KitsuneTone.Project"
  WriteRegStr HKCR ".ktt" "" "KitsuneTone.Template"
  WriteRegStr HKCR "KitsuneTone.Project" "" "狐Tone プロジェクト"
  WriteRegStr HKCR "KitsuneTone.Project\DefaultIcon" "" "$INSTDIR\KitsuneTone.exe,1"
  WriteRegStr HKCR "KitsuneTone.Project\shell\open\command" "" '"$INSTDIR\KitsuneTone.exe" "%1"'
  WriteRegStr HKCR "KitsuneTone.Template" "" "狐Tone プロジェクトテンプレート"
  WriteRegStr HKCR "KitsuneTone.Template\DefaultIcon" "" "$INSTDIR\KitsuneTone.exe,1"
  WriteRegStr HKCR "KitsuneTone.Template\shell\open\command" "" '"$INSTDIR\KitsuneTone.exe" "%1"'
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\KitsuneTone\狐Tone.lnk"
  Delete "$SMPROGRAMS\KitsuneTone\アンインストール.lnk"
  RMDir "$SMPROGRAMS\KitsuneTone"

  DeleteRegKey HKCR ".ktp"
  DeleteRegKey HKCR ".ktpz"
  DeleteRegKey HKCR ".ktt"
  DeleteRegKey HKCR "KitsuneTone.Project"
  DeleteRegKey HKCR "KitsuneTone.Template"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KitsuneTone"
  DeleteRegKey HKLM "Software\KitsuneTone"

  RMDir /r "$INSTDIR"
SectionEnd

