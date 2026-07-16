INCLUDE(BuildPlugin)

# VST3 hosting is currently implemented for Windows, Linux and macOS,
# but only exercised on Windows so far.
IF(NOT LMMS_BUILD_WIN32 AND NOT LMMS_BUILD_LINUX AND NOT LMMS_BUILD_APPLE)
	RETURN()
ENDIF()

SET(VST3_SDK_DIR "${CMAKE_SOURCE_DIR}/src/3rdparty/vst3sdk")

IF(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp")
	MESSAGE(STATUS "VST3 SDK not found at ${VST3_SDK_DIR} - skipping VST3 plugins")
	RETURN()
ENDIF()

# --- static library with the parts of the Steinberg VST3 SDK needed for hosting ---
FILE(GLOB VST3SDK_BASE_SOURCES
	"${VST3_SDK_DIR}/base/source/*.cpp"
	"${VST3_SDK_DIR}/base/thread/source/*.cpp"
	"${VST3_SDK_DIR}/pluginterfaces/base/*.cpp"
)

SET(VST3SDK_HOSTING_SOURCES
	"${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/common/commonstringconvert.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/connectionproxy.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/processdata.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/utility/stringconvert.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp"
)

IF(LMMS_BUILD_WIN32)
	LIST(APPEND VST3SDK_HOSTING_SOURCES
		"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp"
		"${VST3_SDK_DIR}/public.sdk/source/common/threadchecker_win32.cpp"
	)
ELSEIF(LMMS_BUILD_LINUX)
	LIST(APPEND VST3SDK_HOSTING_SOURCES
		"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp"
		"${VST3_SDK_DIR}/public.sdk/source/common/threadchecker_linux.cpp"
	)
ELSEIF(LMMS_BUILD_APPLE)
	LIST(APPEND VST3SDK_HOSTING_SOURCES
		"${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm"
	)
ENDIF()

ADD_LIBRARY(vst3sdkhosting STATIC
	${VST3SDK_BASE_SOURCES}
	${VST3SDK_HOSTING_SOURCES}
)
TARGET_INCLUDE_DIRECTORIES(vst3sdkhosting PUBLIC "${VST3_SDK_DIR}")

# --- ARA SDK headers (experimental ARA detection) ---
SET(ARA_SDK_DIR "${CMAKE_SOURCE_DIR}/src/3rdparty/ara")
IF(EXISTS "${ARA_SDK_DIR}/ARA_API/ARAVST3.h")
	TARGET_INCLUDE_DIRECTORIES(vst3sdkhosting PUBLIC "${ARA_SDK_DIR}")
	# ARA_ENABLE_INTERFACE_ASSERTS=0 avoids the debug-only ARAInterfaceAssert
	# reference; ARA_ENABLE_DEBUG_OUTPUT=0 keeps the library free of stdio deps
	TARGET_COMPILE_DEFINITIONS(vst3sdkhosting PUBLIC
		LMMS_HAVE_ARA=1
		ARA_ENABLE_INTERFACE_ASSERTS=0
		ARA_ENABLE_DEBUG_OUTPUT=0
	)
	TARGET_SOURCES(vst3sdkhosting PRIVATE
		"${ARA_SDK_DIR}/ARA_Library/Debug/ARADebug.c"
		"${ARA_SDK_DIR}/ARA_Library/Dispatch/ARAHostDispatch.cpp"
	)
ENDIF()

# the SDK is not C++20-clean (std::filesystem::path::u8string), build it as C++17
SET_PROPERTY(TARGET vst3sdkhosting PROPERTY CXX_STANDARD 17)
SET_PROPERTY(TARGET vst3sdkhosting PROPERTY CXX_STANDARD_REQUIRED ON)
TARGET_COMPILE_DEFINITIONS(vst3sdkhosting PUBLIC
	$<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>
)
SET_PROPERTY(TARGET vst3sdkhosting PROPERTY POSITION_INDEPENDENT_CODE ON)

IF(LMMS_BUILD_WIN32)
	TARGET_LINK_LIBRARIES(vst3sdkhosting PUBLIC shell32 ole32 user32)
ELSEIF(LMMS_BUILD_APPLE)
	TARGET_LINK_LIBRARIES(vst3sdkhosting PUBLIC "-framework CoreFoundation")
ENDIF()

# --- the LMMS shared base library ---
SET(VST3BASE_ARA_SRC "")
IF(EXISTS "${ARA_SDK_DIR}/ARA_API/ARAVST3.h")
	SET(VST3BASE_ARA_SRC Vst3AraHost.cpp Vst3AraHost.h)
ENDIF()

BUILD_PLUGIN(vst3base
	vst3_base.cpp
	Vst3Plugin.cpp
	Vst3Plugin.h
	Vst3PluginView.cpp
	Vst3PluginView.h
	${VST3BASE_ARA_SRC}
	MOCFILES Vst3Plugin.h
	EXPORT_BASE_NAME vst3base
	LINK SHARED
)

TARGET_LINK_LIBRARIES(vst3base vst3sdkhosting)
TARGET_INCLUDE_DIRECTORIES(vst3base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
