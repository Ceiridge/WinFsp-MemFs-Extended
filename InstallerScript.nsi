!include x64.nsh
!include "MUI2.nsh"

Name "Memefs"
!define INSTALLATIONNAME "Memefs"
OutFile "Memefs-Installer.exe"

!define FILE_SOURCE "x64\Release"
InstallDir $PROGRAMFILES64\memefs

!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\Readme-Installation.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show readme on how to launch ${INSTALLATIONNAME} on boot"

!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function ".onInit"
	${IfNot} ${RunningX64}
		MessageBox MB_OK "Memefs can only be installed on 64-bit operating systems."
		Quit
	${EndIf}
FunctionEnd

Section "" Main
	SetOutPath $INSTDIR
	SetRegView 64

	File Readme-Installation.txt
	File ${FILE_SOURCE}\memefs-x64.exe
	# File ${FILE_SOURCE}\memefs-x64.pdb
	File ${FILE_SOURCE}\winfsp-x64.dll

	WriteUninstaller $INSTDIR\uninstall.exe
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALLATIONNAME}" "DisplayName" "${INSTALLATIONNAME}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALLATIONNAME}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALLATIONNAME}" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALLATIONNAME}" "NoRepair" 1
SectionEnd

Section "WinFsp Launcher Service" LauncherSvc
	WriteRegStr HKLM "SOFTWARE\WOW6432Node\WinFsp\Services\${INSTALLATIONNAME}" "Executable" "$INSTDIR\memefs-x64.exe"
	WriteRegStr HKLM "SOFTWARE\WOW6432Node\WinFsp\Services\${INSTALLATIONNAME}" "CommandLine" "-i -F NTFS -l Ram -u %1 -m %2"
	WriteRegStr HKLM "SOFTWARE\WOW6432Node\WinFsp\Services\${INSTALLATIONNAME}" "Security" "D:P(A;;RPWPLC;;;WD)"
	WriteRegDWORD HKLM "SOFTWARE\WOW6432Node\WinFsp\Services\${INSTALLATIONNAME}" "JobControl" 1
SectionEnd


Section "Uninstall"
	DeleteRegKey HKLM "SOFTWARE\WOW6432Node\WinFsp\Services\${INSTALLATIONNAME}"

	Delete $INSTDIR\Readme-Installation.txt
	Delete $INSTDIR\memefs-x64.exe
	# Delete $INSTDIR\memefs-x64.pdb
	Delete $INSTDIR\winfsp-x64.dll

	Delete $INSTDIR\uninstall.exe
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALLATIONNAME}"

	RMDir $INSTDIR
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${Main} "Memefs itself"
	!insertmacro MUI_DESCRIPTION_TEXT ${LauncherSvc} "Enables the use with launchctl"
!insertmacro MUI_FUNCTION_DESCRIPTION_END
