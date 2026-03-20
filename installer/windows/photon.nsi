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

Section "Install"
    SetOutPath "$INSTDIR"
    File "${WORKDIR}\photon.exe"
    File "${WORKDIR}\SDL2.dll"
    File "${WORKDIR}\SDL2_image.dll"

    ; Start Menu
    CreateDirectory "$SMPROGRAMS\Photon"
    CreateShortcut "$SMPROGRAMS\Photon\Photon.lnk" "$INSTDIR\photon.exe"
    CreateShortcut "$SMPROGRAMS\Photon\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\Photon.lnk" "$INSTDIR\photon.exe"

    ; File associations (.jpg .jpeg .png .bmp .gif .webp)
    !macro AssocExt EXT
        WriteRegStr HKCU "Software\Classes\.${EXT}" "" "PhotonImageFile"
        WriteRegStr HKCU "Software\Classes\.${EXT}\OpenWithProgids" "PhotonImageFile" ""
    !macroend
    !insertmacro AssocExt "jpg"
    !insertmacro AssocExt "jpeg"
    !insertmacro AssocExt "png"
    !insertmacro AssocExt "bmp"
    !insertmacro AssocExt "gif"
    !insertmacro AssocExt "webp"

    ; Register app
    WriteRegStr HKCU "Software\Classes\PhotonImageFile" "" "Image File"
    WriteRegStr HKCU "Software\Classes\PhotonImageFile\shell\open\command" "" '"$INSTDIR\photon.exe" "%1"'

    ; Right-click "Open with Photon"
    WriteRegStr HKCU "Software\Classes\*\shell\Open with Photon" "" "Open with Photon"
    WriteRegStr HKCU "Software\Classes\*\shell\Open with Photon\command" "" '"$INSTDIR\photon.exe" "%1"'

    ; Uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" "DisplayName" "Photon Image Viewer"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon" "UninstallString" "$INSTDIR\uninstall.exe"

    ; Notify Windows of association change
    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\photon.exe"
    Delete "$INSTDIR\SDL2.dll"
    Delete "$INSTDIR\SDL2_image.dll"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    Delete "$DESKTOP\Photon.lnk"
    Delete "$SMPROGRAMS\Photon\Photon.lnk"
    Delete "$SMPROGRAMS\Photon\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Photon"

    DeleteRegKey HKCU "Software\Classes\PhotonImageFile"
    DeleteRegKey HKCU "Software\Classes\*\shell\Open with Photon"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Photon"
    DeleteRegKey HKCU "Software\Classes\.jpg\OpenWithProgids"
    DeleteRegKey HKCU "Software\Classes\.jpeg\OpenWithProgids"
    DeleteRegKey HKCU "Software\Classes\.png\OpenWithProgids"
    DeleteRegKey HKCU "Software\Classes\.bmp\OpenWithProgids"
    DeleteRegKey HKCU "Software\Classes\.gif\OpenWithProgids"
    DeleteRegKey HKCU "Software\Classes\.webp\OpenWithProgids"

    System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd
