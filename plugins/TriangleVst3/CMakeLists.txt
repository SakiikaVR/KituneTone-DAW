# KitsuneTone's bundled default sound source, built as a real, standalone VST3
# module (TriangleSynth.vst3) from the vendored Steinberg VST3 SDK. It is copied
# into <install>/VST3/ so the app auto-discovers it - no external/system folder.

IF(NOT LMMS_BUILD_WIN32)
	# only bundled for Windows so far (matches the VST3 hosting support)
	RETURN()
ENDIF()

SET(VST3_SDK_DIR "${CMAKE_SOURCE_DIR}/src/3rdparty/vst3sdk")
IF(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp")
	MESSAGE(STATUS "VST3 SDK not found - skipping bundled TriangleSynth.vst3")
	RETURN()
ENDIF()

# base + interface sources shared by any VST3 module
FILE(GLOB TRIANGLE_SDK_BASE
	"${VST3_SDK_DIR}/base/source/*.cpp"
	"${VST3_SDK_DIR}/base/thread/source/*.cpp"
	"${VST3_SDK_DIR}/pluginterfaces/base/*.cpp"
)

# plug-in-side helpers (SingleComponentEffect + the factory / module entry)
SET(TRIANGLE_SDK_PLUGIN
	# NOTE: vstsinglecomponenteffect.cpp #includes vsteditcontroller.cpp itself
	# (with a setState->setEditorState rename), so it must NOT be listed again.
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstcomponentbase.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstbus.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstparameters.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/common/pluginview.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/main/pluginfactory.cpp"
	"${VST3_SDK_DIR}/public.sdk/source/main/dllmain.cpp"
)

ADD_LIBRARY(TriangleSynthVst3 MODULE
	TriangleSynth.cpp
	TriangleSynth.def
	${TRIANGLE_SDK_BASE}
	${TRIANGLE_SDK_PLUGIN}
)

TARGET_INCLUDE_DIRECTORIES(TriangleSynthVst3 PRIVATE "${VST3_SDK_DIR}")

# the SDK is not C++20-clean; build the whole module as C++17
SET_PROPERTY(TARGET TriangleSynthVst3 PROPERTY CXX_STANDARD 17)
SET_PROPERTY(TARGET TriangleSynthVst3 PROPERTY CXX_STANDARD_REQUIRED ON)
TARGET_COMPILE_DEFINITIONS(TriangleSynthVst3 PRIVATE
	$<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>
)

SET_TARGET_PROPERTIES(TriangleSynthVst3 PROPERTIES
	PREFIX ""
	OUTPUT_NAME "TriangleSynth"
	SUFFIX ".vst3"
)

TARGET_LINK_LIBRARIES(TriangleSynthVst3 PRIVATE shell32 ole32 user32)
