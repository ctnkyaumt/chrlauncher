; launchbro installer
; NSIS script

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define APP_NAME "launchbro"
!define APP_VERSION "2.7"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "launchbro-setup.exe"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
RequestExecutionLevel admin

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; Copy binaries
    File /r "dist\launchbro\*"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Create start menu shortcuts
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\64\launchbro.exe"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Registry entries
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayIcon" "$INSTDIR\64\launchbro.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "ctnkyaumt"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"
SectionEnd

Section "Uninstall"
    ; Close launchbro / chrlauncher and bundled browser processes from this install root
    ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "$roots=@(''$INSTDIR'');$legacy=Join-Path ([System.IO.Path]::GetDirectoryName(''$INSTDIR'')) ''chrlauncher'';if(Test-Path $legacy){$roots+=$legacy};Get-CimInstance Win32_Process | Where-Object { $path=$_.ExecutablePath; $path -and @($roots | Where-Object { $path.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0 } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"'
    Sleep 1500

    ; Remove shortcuts
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"

    ; Ask about profiles
    MessageBox MB_YESNO|MB_ICONQUESTION "Do you want to keep your browser profiles?" IDYES KeepProfiles

    ; Delete everything
    RMDir /r "$INSTDIR"
    Goto CleanupRegistry

KeepProfiles:
    ; Delete only app files, keep bin and profile folders
    Delete "$INSTDIR\64\launchbro.exe"
    Delete "$INSTDIR\32\launchbro.exe"
    Delete "$INSTDIR\64\*.dll"
    Delete "$INSTDIR\32\*.dll"
    Delete "$INSTDIR\uninstall.exe"
    Delete "$INSTDIR\*.ini"
    Delete "$INSTDIR\*.lng"
    Delete "$INSTDIR\*.txt"
    Delete "$INSTDIR\*.reg"
    Delete "$INSTDIR\*.bat"
    RMDir /r "$INSTDIR\i18n"
    ; bin and profile folders remain

CleanupRegistry:
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
SectionEnd

