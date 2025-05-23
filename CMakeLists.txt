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
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

set(MIXERX_VERSION_STRING "2.7.0")

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

if(NOT NINTENDO_WII AND NOT NINTENDO_WIIU AND NOT NINTENDO_3DS AND NOT NINTENDO_SWITCH)
    target_link_libraries(mixerx PRIVATE ncurses)
endif()

add_dependencies(mixerx SDLMixerX_Local)

if(NINTENDO_WII)
    string(TIMESTAMP MIXERX_WIIMETA_RELEASE_DATE "%Y%m%d")
    if(NOT "${MIXERX_VERSION_REL}" STREQUAL "")
        set(MIXERX_WIIMETA_SHORT_DESC "git ${GIT_BRANCH} #${GIT_COMMIT_HASH}")
    else()
        set(MIXERX_WIIMETA_SHORT_DESC "A simple demo of MixerX for Wii")
    endif()

    configure_file(${CMAKE_SOURCE_DIR}/resources/wii/icon.png ${CMAKE_BINARY_DIR}/package/icon.png COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/resources/wii/meta.xml.in ${CMAKE_BINARY_DIR}/package/meta.xml)

    ogc_create_dol(mixerx)

    add_custom_command(TARGET mixerx POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy
                           "${CMAKE_BINARY_DIR}/mixerx.dol"
                           "${CMAKE_BINARY_DIR}/package/boot.dol"
                       COMMENT "Copying mixerx executable into package")
elseif(NINTENDO_WIIU)
    string(TIMESTAMP MIXERX_WIIMETA_RELEASE_DATE "%Y%m%d")
    if(NOT "${MIXERX_VERSION_REL}" STREQUAL "")
        set(MIXERX_WIIMETA_SHORT_DESC "git ${GIT_BRANCH} #${GIT_COMMIT_HASH}")
    else()
        set(MIXERX_WIIMETA_SHORT_DESC "A simple demo of MixerX for Wii U")
    endif()

    configure_file(${CMAKE_SOURCE_DIR}/resources/wiiu/icon.png ${CMAKE_BINARY_DIR}/icon.png COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/resources/wiiu/meta.xml.in ${CMAKE_BINARY_DIR}/meta.xml)

    target_compile_definitions(mixerx PRIVATE "-D_GNU_SOURCE=1")

    wut_create_rpx(mixerx)
    wut_create_wuhb(mixerx
        CONTENT
        NAME "MixerX Player v${MIXERX_VERSION_STRING}"
        SHORTNAME "MixerX"
        AUTHOR "Wohlstand"
        ICON "${CMAKE_SOURCE_DIR}/resources/switch/mixerx-logo.png"
        TVSPLASH "${CMAKE_SOURCE_DIR}/resources/wiiu/wuhb-splash.png"
        DRCSPLASH "${CMAKE_SOURCE_DIR}/resources/wiiu/wuhb-splash.png"
    )
elseif(NINTENDO_3DS)
    ctr_generate_smdh(OUTPUT "mixerx.smdh"
        NAME "MixerX Demo Player"
        DESCRIPTION "Demo tool of MixerX library"
        AUTHOR "Wohlstand"
        ICON "${CMAKE_SOURCE_DIR}/resources/icon/cat_musplay_48x48.png"
    )
    ctr_create_3dsx(mixerx SMDH "mixerx.smdh")
elseif(NINTENDO_SWITCH)
    find_program(NACPTOOL nacptool REQUIRED)
    message("-- napctool found: ${NACPTOOL}")

    find_program(ELF2NRO elf2nro REQUIRED)
    message("-- elf2nro found: ${ELF2NRO}")

    add_custom_target(mixerx_nacp ALL
        "${NACPTOOL}" --create
        "MixerX"
        "WohlSoft"
        "${MIXERX_VERSION_STRING}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mixerx.nacp"
        DEPENDS mixerx
        BYPRODUCTS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mixerx.nacp"
    )

    add_custom_target(thextech_nro ALL
        "${ELF2NRO}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mixerx.elf"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mixerx.nro"
        "--icon=${CMAKE_SOURCE_DIR}/resources/switch/mixerx-logo.jpg"
        "--nacp=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mixerx.nacp"
        DEPENDS mixerx mixerx_nacp
    )
endif()
