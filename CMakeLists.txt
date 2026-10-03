cmake_minimum_required(VERSION 3.19)

# 64-bit build of the GroundElapsedTime pipe exporter for OpenOrbiter 2024 +
# NASSP 9. The Win32 beta90 Visual Studio solution is a separate, unchanged
# build path; this file is used only for x64.
project(GroundElapsedTime VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This CMake build is x64 only. Use GroundElapsedTime.sln for the Win32 beta90 build.")
endif()

# Dependencies live in the git-ignored .deps folder (see setup-deps-x64.ps1).
set(GET_NASSP9_SRC_DIR "${CMAKE_SOURCE_DIR}/.deps/NASSP-9/Orbitersdk/samples/ProjectApollo"
    CACHE PATH "NASSP 9 ProjectApollo source folder (headers are read from here, never modified)")
set(GET_ORBITERSDK_DIR "${CMAKE_SOURCE_DIR}/.deps/OrbiterSDK-2024/Orbitersdk"
    CACHE PATH "OpenOrbiter 2024 x64 Orbitersdk folder (include + lib)")

foreach(p "${GET_NASSP9_SRC_DIR}/src_csm/saturn.h" "${GET_ORBITERSDK_DIR}/include/Orbitersdk.h"
          "${GET_ORBITERSDK_DIR}/lib/Orbiter.lib" "${GET_ORBITERSDK_DIR}/lib/Orbitersdk.lib")
    if(NOT EXISTS "${p}")
        message(FATAL_ERROR "Missing ${p}. Run setup-deps-x64.ps1 first.")
    endif()
endforeach()

set(SRC "${CMAKE_SOURCE_DIR}/Orbitersdk/samples/GroundElapsedTime/src")

add_library(GroundElapsedTime SHARED
    ${SRC}/GroundElapsedTimePlugin.cpp
    ${SRC}/GroundElapsedTimeCommon.cpp
    ${SRC}/GroundElapsedTimeExport.cpp)

# Same include list as NASSP's own ProjectApolloMFD CMake target.
target_include_directories(GroundElapsedTime PRIVATE
    ${GET_NASSP9_SRC_DIR}/src_aux
    ${GET_NASSP9_SRC_DIR}/src_csm
    ${GET_NASSP9_SRC_DIR}/src_landing
    ${GET_NASSP9_SRC_DIR}/src_launch
    ${GET_NASSP9_SRC_DIR}/src_lm
    ${GET_NASSP9_SRC_DIR}/src_mfd
    ${GET_NASSP9_SRC_DIR}/src_moon
    ${GET_NASSP9_SRC_DIR}/src_rtccmfd
    ${GET_NASSP9_SRC_DIR}/src_saturn
    ${GET_NASSP9_SRC_DIR}/src_sys
    ${GET_ORBITERSDK_DIR}/include
    ${GET_ORBITERSDK_DIR}/XRSound)

target_compile_definitions(GroundElapsedTime PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_link_directories(GroundElapsedTime PRIVATE ${GET_ORBITERSDK_DIR}/lib)
target_link_libraries(GroundElapsedTime PRIVATE Orbiter Orbitersdk User32 Gdi32)

set_target_properties(GroundElapsedTime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-output-x64/Modules/Plugin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build-output-x64/Modules/Plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build-output-x64/Modules/Plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/build-output-x64/Modules/Plugin")


# Optional: pipe protocol test harness (real exporter code, stubbed Orbiter API).
option(GET_BUILD_PIPE_HARNESS "Build tests/pipe_harness" OFF)
if(GET_BUILD_PIPE_HARNESS)
    add_executable(pipe_harness tests/pipe_harness/pipe_harness.cpp ${SRC}/GroundElapsedTimeExport.cpp)
    target_include_directories(pipe_harness PRIVATE tests/pipe_harness/stub ${SRC})
    target_compile_definitions(pipe_harness PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(pipe_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/harness")
endif()
