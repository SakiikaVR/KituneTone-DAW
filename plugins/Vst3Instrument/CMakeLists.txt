INCLUDE(BuildPlugin)

IF(NOT TARGET vst3base)
	RETURN()
ENDIF()

BUILD_PLUGIN(vst3instrument
	Vst3Instrument.cpp
	Vst3Instrument.h
	MOCFILES Vst3Instrument.h
)

TARGET_LINK_LIBRARIES(vst3instrument vst3base)
