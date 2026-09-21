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
set(VITA_VERSION "00.90")

# Perfil de aplicación para reproducción de música en segundo plano.
# 17338376 = 0x01089008. Entre sus flags están NO SUSPEND DEACTIVATE
# (0x00000008) y DISABLE SYSTEM SUSPEND (0x00080000).
# Este último es la diferencia clave respecto al intento anterior 0x808:
# evita que Shell congele el proceso al volver al LiveArea.
set(VITA_MKSFOEX_FLAGS
    "-d ATTRIBUTE=17338376"
    "-d ATTRIBUTE_MINOR=0"
    "-s CATEGORY=gd"
    "-s CONTENT_ID=HB0000-PENGPLR01_00-PENGPLAYER000000"
    "-d EBOOT_APP_MEMSIZE=67108864"
    "-d EBOOT_PHY_MEMSIZE=4259840"
    "-d GC_RO_SIZE=0"
    "-d GC_RW_SIZE=0"
    "-d PARENTAL_LEVEL=1"
    "-s PSP2_DISP_VER=03.600"
    "-d PSP2_SYSTEM_VER=56623104"
    "-d REGION_DENY=0"
    "-d SAVEDATA_MAX_SIZE=0"
    "-s VERSION=00.90"
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fno-rtti")

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/browser.cpp
    src/image_browser.cpp
    src/audio/audio_player.cpp
    src/media/metadata.cpp
    src/media/cover_art.cpp
    src/library/library_manager.cpp
    src/preferences/preferences_manager.cpp
    src/visualizer/audio_visualizer.cpp
    src/playlists/playlist_manager.cpp
    src/smart/smart_library_manager.cpp
    src/ui/text_input.cpp
    src/system/background_playback_manager.cpp
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
    SceIme_stub
    SceAppUtil_stub
    SceAudio_stub
    ScePower_stub
    SceProcessmgr_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
