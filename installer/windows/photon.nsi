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

    ; Runtime DLLs
    File /nonfatal "${WORKDIR}\libgcc_s_seh-1.dll"
    File /nonfatal "${WORKDIR}\libwinpthread-1.dll"
    File /nonfatal "${WORKDIR}\libstdc++-6.dll"
    File /nonfatal "${WORKDIR}\libjpeg-62.dll"
    File /nonfatal "${WORKDIR}\libpng16-16.dll"
    File /nonfatal "${WORKDIR}\libwebp-7.dll"
    File /nonfatal "${WORKDIR}\libfreetype-6.dll"
    File /nonfatal "${WORKDIR}\zlib1.dll"

    ; JPEG XL and dependencies
    File /nonfatal "${WORKDIR}\libjxl.dll"
    File /nonfatal "${WORKDIR}\libjxl_threads.dll"
    File /nonfatal "${WORKDIR}\libbrotlicommon.dll"
    File /nonfatal "${WORKDIR}\libbrotlidec.dll"
    File /nonfatal "${WORKDIR}\libhwy.dll"

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
                       "DisplayVersion"  "1.0.1"
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

    ; Application files
    Delete "$INSTDIR\photon.exe"
    Delete "$INSTDIR\SDL2.dll"
    Delete "$INSTDIR\SDL2_image.dll"
    Delete "$INSTDIR\SDL2_ttf.dll"

    ; Runtime DLLs
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libjpeg-62.dll"
    Delete "$INSTDIR\libpng16-16.dll"
    Delete "$INSTDIR\libwebp-7.dll"
    Delete "$INSTDIR\libfreetype-6.dll"
    Delete "$INSTDIR\zlib1.dll"

    ; JPEG XL and dependencies
    Delete "$INSTDIR\libjxl.dll"
    Delete "$INSTDIR\libjxl_threads.dll"
    Delete "$INSTDIR\libbrotlicommon.dll"
    Delete "$INSTDIR\libbrotlidec.dll"
    Delete "$INSTDIR\libhwy.dll"

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
