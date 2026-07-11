INCLUDE(BuildPlugin)

IF(NOT TARGET vst3base)
	RETURN()
ENDIF()

BUILD_PLUGIN(vst3effect
	Vst3Effect.cpp
	Vst3Effect.h
	Vst3SubPluginFeatures.cpp
	Vst3SubPluginFeatures.h
	MOCFILES Vst3Effect.h
)

TARGET_LINK_LIBRARIES(vst3effect vst3base)
