SET(WIN_PLATFORM mingw)
if(LMMS_MSVC_YEAR)
	SET(WIN_PLATFORM "msvc${LMMS_MSVC_YEAR}")
endif()

# the final slash needs to be flipped for CPACK_PACKAGE_ICON to work:
#   https://cmake.org/pipermail/cmake/2008-June/022085.html
SET(CPACK_PACKAGE_ICON                  "${CMAKE_SOURCE_DIR}/cmake/nsis\\\\nsis_branding.bmp")
SET(CPACK_NSIS_MUI_ICON                 "${CMAKE_SOURCE_DIR}/cmake/nsis/icon.ico")
SET(CPACK_NSIS_INSTALLED_ICON_NAME      "KitsuneTone.exe" PARENT_SCOPE)
SET(CPACK_NSIS_DISPLAY_NAME             "${PROJECT_NAME_UCASE} ${VERSION}" PARENT_SCOPE)
SET(CPACK_NSIS_HELP_LINK                "${PROJECT_URL}" PARENT_SCOPE)
SET(CPACK_NSIS_URL_INFO_ABOUT           "${PROJECT_URL}" PARENT_SCOPE)
SET(CPACK_NSIS_CONTACT                  "${PROJECT_EMAIL}" PARENT_SCOPE)
SET(CPACK_PACKAGE_EXECUTABLES           "KitsuneTone.exe;狐Tone" PARENT_SCOPE)
SET(CPACK_NSIS_MENU_LINKS               "KitsuneTone.exe;狐Tone" PARENT_SCOPE)
SET(CPACK_NSIS_DEFINES                  "
  !addincludedir ${CMAKE_SOURCE_DIR}/cmake/nsis
  !include FileAssociation.nsh
  !include LogicLib.nsh
  !include WinVer.nsh")
SET(CPACK_PACKAGE_FILE_NAME             "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-win32")
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS   "
  \\\${registerExtension} \\\"$INSTDIR\\\\KitsuneTone.exe\\\" \\\".ktp\\\" \\\"KitsuneTone Project\\\"
  \\\${registerExtension} \\\"$INSTDIR\\\\KitsuneTone.exe\\\" \\\".ktpz\\\" \\\"KitsuneTone Project (compressed)\\\"
  \\\${registerExtension} \\\"$INSTDIR\\\\KitsuneTone.exe\\\" \\\".ktt\\\" \\\"KitsuneTone Project Template\\\"
  \\\${IfNot} \\\${AtMostWin7}
    WriteRegDWORD HKLM \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\SideBySide\\\" \\\"PreferExternalManifest\\\" \\\"1\\\"
  \\\${EndIf}
  " PARENT_SCOPE)
SET(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  \\\${unregisterExtension} \\\".ktp\\\" \\\"KitsuneTone Project\\\"
  \\\${unregisterExtension} \\\".ktpz\\\" \\\"KitsuneTone Project (compressed)\\\"
  \\\${unregisterExtension} \\\".ktt\\\" \\\"KitsuneTone Project Template\\\"
  DeleteRegKey HKCR \\\"KitsuneTone Project\\\"
  DeleteRegKey HKCR \\\"KitsuneTone Project (compressed)\\\"
  DeleteRegKey HKCR \\\"KitsuneTone Project Template\\\"
  " PARENT_SCOPE)

IF(WIN64)
	if(IS_ARM64)
		set(CPACK_PACKAGE_FILE_NAME      "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-arm64")
	else()
		set(CPACK_PACKAGE_FILE_NAME      "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-win64")
	endif()
	SET(CPACK_INSTALL_FIX "$PROGRAMFILES64\\\\${CPACK_PACKAGE_INSTALL_DIRECTORY}\\\\")
	SET(CPACK_NSIS_DEFINES "
		${CPACK_NSIS_DEFINES} 
		InstallDir ${CPACK_INSTALL_FIX}
		")
ENDIF()

# Setup missing parent scopes
SET(CPACK_PACKAGE_FILE_NAME             "${CPACK_PACKAGE_FILE_NAME}" PARENT_SCOPE)
SET(CPACK_NSIS_DEFINES                  "${CPACK_NSIS_DEFINES}" PARENT_SCOPE)
SET(CPACK_PACKAGE_ICON                  "${CPACK_PACKAGE_ICON}" PARENT_SCOPE)
SET(CPACK_NSIS_MUI_ICON                 "${CPACK_NSIS_MUI_ICON}" PARENT_SCOPE)

# Disable cpack's strip for historic reasons
set(CPACK_STRIP_FILES_ORIG "${CPACK_STRIP_FILES}" PARENT_SCOPE)
set(CPACK_STRIP_FILES FALSE PARENT_SCOPE)

if(CPACK_DEBUG)
	# CMake 3.19+
	set(CPACK_NSIS_EXECUTABLE_PRE_ARGUMENTS "-V4")
endif()

# Windows resource compilers
CONFIGURE_FILE("lmms.rc.in"             "${CMAKE_BINARY_DIR}/lmms.rc")
CONFIGURE_FILE("zynaddsubfx.rc.in"      "${CMAKE_BINARY_DIR}/plugins/ZynAddSubFx/zynaddsubfx.rc")

INSTALL(FILES "lmms.exe.manifest" DESTINATION .)
INSTALL(FILES "lmms.VisualElementsManifest.xml" DESTINATION .)
INSTALL(DIRECTORY "assets" DESTINATION .)
