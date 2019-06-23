
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

target_link_libraries(core ole32 gdi32 user32 shell32 ole32 dwrite d2d1 usp10 ws2_32 iphlpapi SDL2 uxtheme comctl32 ws2_32 opengl32)


file(GLOB CLR_FILES
				"Vanguard/Helpers.hpp"
				"Vanguard/VanguardClient.cpp"
        )

include_external_msproject(
    CorruptCore ${PROJECT_SOURCE_DIR}/../../RTCV/Source/Libraries/CorruptCore/CorruptCore.csproj
    TYPE B5633A86-DC66-4723-88EB-7DFC95458072)
include_external_msproject(
    NetCore ${PROJECT_SOURCE_DIR}/../../RTCV/Source/Libraries/NetCore/NetCore.csproj
    TYPE 7E106AEC-5614-4206-9E4D-25759C90E8AE)
include_external_msproject(
    Vanguard ${PROJECT_SOURCE_DIR}/../../RTCV/Source/Libraries/Vanguard/Vanguard.csproj
    TYPE 364C705E-B7AB-4A94-A59B-88899CE65958)

set_source_files_properties(${CLR_FILES} PROPERTIES COMPILE_FLAGS "/clr /EHa /Zc:twoPhase-")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES_COPY_LOCAL ON )
set_target_properties(${PROJECT_NAME} PROPERTIES DOTNET_TARGET_FRAMEWORK_VERSION "v4.7.1")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_CorruptCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/CorruptCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_NetCore "${PROJECT_SOURCE_DIR}/../../RTCV/Build/NetCore.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCE_Vanguard "${PROJECT_SOURCE_DIR}/../../RTCV/Build/Vanguard.dll")
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DOTNET_REFERENCES "System;System.Windows.Forms")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
# Narrysmod
# Copy RTCV dlls and pdbs for easier dev purposes
set (RTC_DLL_DIR "${PROJECT_SOURCE_DIR}./../RTCV/Build/")
file(GLOB rtcdlls RELATIVE "${RTC_DLL_DIR}" "${RTC_DLL_DIR}/*.dll")
file(GLOB rtcpdbs RELATIVE "${RTC_DLL_DIR}" "${RTC_DLL_DIR}/*.pdb")

foreach(dll ${rtcdlls})
configure_file("${RTC_DLL_DIR}/${dll}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${dll}" COPYONLY)
endforeach()

foreach(pdb ${rtcpdbs})
configure_file("${RTC_DLL_DIR}/${pdb}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${pdb}" COPYONLY)
endforeach()
