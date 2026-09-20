cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain file")
    else()
        message(FATAL_ERROR "Define VITASDK before configuring the project.")
    endif()
endif()

project(PengPlayer C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "PengPlayer")
set(VITA_TITLEID "PENGPLR01")
set(VITA_VERSION "00.20")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fno-rtti")

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/browser.cpp
    src/audio/audio_player.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
    vita2d

    # libsndfile + codecs usados por el paquete estatico de VitaSDK.
    sndfile
    mp3lame
    mpg123
    opus
    vorbis
    ogg
    vorbisenc
    vorbisfile
    FLAC

    freetype
    png
    jpeg
    z
    m

    SceDisplay_stub
    SceGxm_stub
    SceSysmodule_stub
    SceCtrl_stub
    ScePgf_stub
    SceIofilemgr_stub
    SceAppMgr_stub
    SceCommonDialog_stub
    SceAudio_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
