cmake_minimum_required(VERSION 3.5)

project(MixerXMusPlay LANGUAGES C CXX)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(ExternalProject)
include(CheckLibraryExists)
include(CheckFunctionExists)
include(GNUInstallDirs)
include(TestBigEndian)
include(CheckIncludeFile)

set(CMAKE_CXX_STANDARD 11)

if(ANDROID)
    set(DEPENDENCIES_INSTALL_DIR ${CMAKE_BINARY_DIR}/output-deps)
else()
    set(DEPENDENCIES_INSTALL_DIR ${CMAKE_BINARY_DIR}/output)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output/bin)

set(PGE_INSTALL_DIRECTORY "MixerXPlayer")

set(USE_SYSTEM_LIBS_DEFAULT OFF)

if(HAIKU OR VITA OR NINTENDO_SWITCH OR NINTENDO_WII OR NINTENDO_WIIU OR XTECH_MACOSX_TIGER)
    set(USE_SYSTEM_SDL2_DEFAULT ON)  # Should be used on Haiku, own SDL2 Haiku build on CMake is broken
else()
    set(USE_SYSTEM_SDL2_DEFAULT ${USE_SYSTEM_LIBS_DEFAULT})
endif()

option(USE_SYSTEM_SDL2 "Use SDL2 from a system even prefering system libraries" ${USE_SYSTEM_SDL2_DEFAULT})
option(USE_STATIC_LIBC "Link libc and libstdc++ statically" OFF)

if(NINTENDO_3DS OR NINTENDO_WII OR NINTENDO_WIIU OR NINTENDO_DS)
    set(USE_STATIC_LIBC ON)
endif()

if(USE_STATIC_LIBC)
    set(BUILD_SHARED_LIBS OFF)
endif()

include(cmake/build_props.cmake)
include(cmake/library_zlib.cmake)
include(cmake/library_SDLMixerX.cmake)


add_executable(mixerx
    src/playmus.c
    src/fx/spc_echo.cpp
)


test_big_endian(THEXTECH_IS_BIG_ENDIAN)
if(THEXTECH_IS_BIG_ENDIAN)
    message("== Target processor endianess: BIG ENDIAN")
    target_compile_definitions(mixerx PRIVATE -DSOUND_FX_BIG_ENDIAN)
else()
    message("== Target processor endianess: LITTLE ENDIAN")
endif()

if(PGE_SHARED_SDLMIXER)
    target_link_libraries(mixerx PRIVATE PGE_SDLMixerX)
else()
    target_link_libraries(mixerx PRIVATE PGE_SDLMixerX_static PGE_ZLib)
endif()

if(NOT NINTENDO_WII AND NOT NINTENDO_WIIU)
    target_link_libraries(mixerx PRIVATE ncurses)
endif()

add_dependencies(mixerx SDLMixerX_Local)

if(NINTENDO_WII)
    ogc_create_dol(mixerx)
elseif(NINTENDO_WIIU)
    wut_create_rpx(mixerx)
endif()
