
project(core)

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
				"Vanguard/MelonMemoryDomains.h"
				"Vanguard/MelonMemoryDomains.cpp"
				"Vanguard/Helpers.hpp"
				"Vanguard/VanguardClient.cpp"
			)

set_source_files_properties(${CLR_FILES} PROPERTIES COMPILE_FLAGS "/clr /EHa /Zc:twoPhase-")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES_COPY_LOCAL ON )
set_target_properties(${PROJECT_NAME} PROPERTIES DOTNET_TARGET_FRAMEWORK_VERSION "v4.7.1")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_CorruptCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/CorruptCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_NetCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/NetCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_NetCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/RTCV.Common.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_Vanguard "${PROJECT_SOURCE_DIR}/../../RTCV/Build/Vanguard.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES "System;System.Windows.Forms")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
# Narrysmod
# Copy RTCV dlls and pdbs for easier dev purposes
set (RTC_DLL_DIR "${PROJECT_SOURCE_DIR}/../../RTCV/Build/")
file(GLOB rtcdlls RELATIVE "${RTC_DLL_DIR}" "${RTC_DLL_DIR}/*.dll")
file(GLOB rtcpdbs RELATIVE "${RTC_DLL_DIR}" "${RTC_DLL_DIR}/*.pdb")

foreach(dll ${rtcdlls})
configure_file("${RTC_DLL_DIR}/${dll}" "${CMAKE_BINARY_DIR}/${dll}" COPYONLY) 
endforeach()

foreach(pdb ${rtcpdbs})
configure_file("${RTC_DLL_DIR}/${pdb}" "${CMAKE_BINARY_DIR}/${pdb}" COPYONLY)
endforeach()

if(NOT DONT_COPY_FIRMWARE)
	configure_file(${CMAKE_SOURCE_DIR}/SDL2.dll ${CMAKE_BINARY_DIR}/SDL2.dll COPYONLY) 
	configure_file(${CMAKE_SOURCE_DIR}/firmware.bin ${CMAKE_BINARY_DIR}/firmware.bin COPYONLY) 
	configure_file(${CMAKE_SOURCE_DIR}/bios7.bin ${CMAKE_BINARY_DIR}/bios7.bin COPYONLY) 
	configure_file(${CMAKE_SOURCE_DIR}/bios9.bin ${CMAKE_BINARY_DIR}/bios9.bin COPYONLY)
endif()

target_link_libraries(core ole32 gdi32 user32 shell32 ole32 dwrite d2d1 usp10 ws2_32 iphlpapi SDL2 uxtheme comctl32 ws2_32 opengl32)

