; Windows installer for Prepress PDF.
; makensis -DVERSION=0.1.0 -DSOURCE=<deployed folder> -DOUTFILE=<setup.exe> installer.nsi

Unicode true
!include "MUI2.nsh"

!define APPNAME "Prepress PDF"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PrepressPdf"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_ICON "app.ico"
!define MUI_UNICON "app.ico"
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetRegView 64
    SetShellVarContext all
    SetOutPath "$INSTDIR"
    File /r "${SOURCE}\*.*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\prepresspdf.exe"

    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\prepresspdf.exe"
    WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    SetRegView 64
    SetShellVarContext all
    Delete "$SMPROGRAMS\${APPNAME}.lnk"
    DeleteRegKey HKLM "${UNINSTKEY}"
    ; Only clear the folder when it really is this app's folder.
    IfFileExists "$INSTDIR\prepresspdf.exe" 0 +2
        RMDir /r "$INSTDIR"
SectionEnd
