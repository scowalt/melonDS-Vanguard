
project(core)

include_directories("C:/Users/Daniel/Documents/deps")
link_directories("C:/Users/Daniel/Documents/deps/SDL_dev/libs")
add_library(core STATIC
	ARM.cpp
	ARMInterpreter.cpp
	ARMInterpreter_ALU.cpp
	ARMInterpreter_Branch.cpp
	ARMInterpreter_LoadStore.cpp
	Config.cpp
	CP15.cpp
	CRC32.cpp
	DMA.cpp
	GPU.cpp
	GPU2D.cpp
	GPU3D.cpp
	GPU3D_OpenGL.cpp
	GPU3D_Soft.cpp
	NDS.cpp
	NDSCart.cpp
	OpenGLSupport.cpp
	RTC.cpp
	Savestate.cpp
	SPI.cpp
	SPU.cpp
	Wifi.cpp
	WifiAP.cpp
	Vanguard/Helpers.hpp
	Vanguard/VanguardClient.cpp
)
file(GLOB CLR_FILES
				"Vanguard/Helpers.hpp"
				"Vanguard/VanguardClient.cpp"
        )
		
set_source_files_properties(${CLR_FILES} PROPERTIES COMPILE_FLAGS "/clr /EHa /Zc:twoPhase-")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES_COPY_LOCAL ON )
set_target_properties(${PROJECT_NAME} PROPERTIES DOTNET_TARGET_FRAMEWORK_VERSION "v4.7.1")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_CorruptCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/CorruptCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_NetCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/NetCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_Vanguard "${PROJECT_SOURCE_DIR}/../../RTCV/Build/Vanguard.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES "System;System.Windows.Forms")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

target_link_libraries(core ole32 gdi32 user32 shell32 ole32 dwrite d2d1 usp10 ws2_32 iphlpapi SDL2 uxtheme comctl32 ws2_32 opengl32)