!include "MUI2.nsh"

Name "Photon Image Viewer"
OutFile "Photon-Setup.exe"
InstallDir "$PROGRAMFILES64\Photon"
InstallDirRegKey HKCU "Software\Photon" ""
RequestExecutionLevel admin

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── File association macro (must be outside Section) ─────────────────────────
!macro AssocExt EXT
    WriteRegStr HKCU "Software\Classes\.${EXT}" "" "PhotonImageFile"
    WriteRegStr HKCU "Software\Classes\.${EXT}\OpenWithProgids" "PhotonImageFile" ""
!macroend

; ── Install ───────────────────────────────────────────────────────────────────
Section "Install"

    SetOutPath "$INSTDIR"

    ; Application files
    File "${WORKDIR}\photon.exe"
    File "${WORKDIR}\SDL2.dll"
    File "${WORKDIR}\SDL2_image.dll"
    File "${WORKDIR}\SDL2_ttf.dll"

    ; Start Menu
    CreateDirectory "$SMPROGRAMS\Photon"
    CreateShortcut "$SMPROGRAMS\Photon\Photon.lnk"     "$INSTDIR\photon.exe"
    CreateShortcut "$SMPROGRAMS\Photon\Uninstall.lnk"  "$INSTDIR\uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\Photon.lnk" "$INSTDIR\photon.exe"

    ; File associations
    !insertmacro AssocExt "jpg"
    !insertmacro AssocExt "jpeg"
    !insertmacro AssocExt "png"
    !insertmacro AssocExt "bmp"
    !insertmacro AssocExt "gif"
    !insertmacro AssocExt "webp"

    ; Register app handler
    WriteRegStr HKCU "Software\Classes\PhotonImageFile" "" "Image File"
    WriteRegStr HKCU "Software\Classes\PhotonImageFile\shell\open\command" "" \
                '"$INSTDIR\photon.exe" "%1"'

    ; Right-click "Open with Photon" on any file
    WriteRegStr HKCU "Software\Classes\*\shell\Open with Photon" "" "Open with Photon"
    WriteRegStr HKCU "Software\Classes\*\shell\Open with Photon\command" "" \
                '"$INSTDIR\photon.exe" "%1"'

    ; Uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Add/Remove Programs entry
    WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "DisplayName"     "Photon Image Viewer"
    WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "DisplayVersion"  "1.0.0"
    WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "Publisher"       "Photon Project"
    WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" \
                       "NoRepair"  1

    ; Notify Windows of association change
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'

SectionEnd

; ── Uninstall ─────────────────────────────────────────────────────────────────
Section "Uninstall"

    ; Files
    Delete "$INSTDIR\photon.exe"
    Delete "$INSTDIR\SDL2.dll"
    Delete "$INSTDIR\SDL2_image.dll"
    Delete "$INSTDIR\SDL2_ttf.dll"
    Delete "$INSTDIR\uninstall.exe"
    RMDir  "$INSTDIR"

    ; Shortcuts
    Delete "$DESKTOP\Photon.lnk"
    Delete "$SMPROGRAMS\Photon\Photon.lnk"
    Delete "$SMPROGRAMS\Photon\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\Photon"

    ; Registry
    DeleteRegKey HKCU "Software\Classes\PhotonImageFile"
    DeleteRegKey HKCU "Software\Classes\*\shell\Open with Photon"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon"
    DeleteRegKey HKCU "Software\Classes\.jpg"
    DeleteRegKey HKCU "Software\Classes\.jpeg"
    DeleteRegKey HKCU "Software\Classes\.png"
    DeleteRegKey HKCU "Software\Classes\.bmp"
    DeleteRegKey HKCU "Software\Classes\.gif"
    DeleteRegKey HKCU "Software\Classes\.webp"

    ; Notify Windows
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'

SectionEnd
