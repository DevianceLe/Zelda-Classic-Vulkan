# zplayer: the music decoders under third_party/ (see third_party/README.md).
# Included from src/port/CMakeLists.txt. Each library builds static, for any
# architecture (x86, x64, arm64), with no Allegro dependency.
#
#   zc_stb_vorbis   STATIC     OGG Vorbis decoder (stb_vorbis.c)
#   zc_minimp3      INTERFACE  MP3 decoder (header only: define MINIMP3_IMPLEMENTATION in one file)
#   zc_gme          STATIC     Game_Music_Emu: NSF, SPC, GBS, VGM, GYM (and the other emulators)
#   zc_dumb         STATIC     DUMB: IT, XM, S3M, MOD (core, helpers, it; no Allegro glue)
#   zc_tsf          INTERFACE  TinySoundFont + TinyMidiLoader (header only), for the Android MIDI synth

set(_zc_audio_quiet)
if(MSVC)
    set(_zc_audio_quiet /W0 /utf-8)
else()
    set(_zc_audio_quiet -w)
endif()

# --- OGG Vorbis ---------------------------------------------------------------
add_library(zc_stb_vorbis STATIC ${ZC_THIRD_PARTY}/stb_vorbis/stb_vorbis.c)
target_include_directories(zc_stb_vorbis PUBLIC ${ZC_THIRD_PARTY}/stb_vorbis)
target_compile_options(zc_stb_vorbis PRIVATE ${_zc_audio_quiet})

# --- MP3 ----------------------------------------------------------------------
add_library(zc_minimp3 INTERFACE)
target_include_directories(zc_minimp3 INTERFACE ${ZC_THIRD_PARTY}/minimp3)

# --- Game_Music_Emu -----------------------------------------------------------
file(GLOB _zc_gme_sources CONFIGURE_DEPENDS ${ZC_THIRD_PARTY}/gme/gme/*.cpp)
# One YM2612 core (VGM/GYM): GENS, as in the GME the Allegro build linked
# (MAME's core is GPL, Nuked differs more from the original sound).
list(REMOVE_ITEM _zc_gme_sources
    ${ZC_THIRD_PARTY}/gme/gme/Ym2612_MAME.cpp
    ${ZC_THIRD_PARTY}/gme/gme/Ym2612_Nuked.cpp)
add_library(zc_gme STATIC ${_zc_gme_sources} ${ZC_THIRD_PARTY}/gme/gme/ext/emu2413.c)
target_include_directories(zc_gme PUBLIC ${ZC_THIRD_PARTY}/gme)
target_include_directories(zc_gme PRIVATE ${ZC_THIRD_PARTY}/gme/gme)
if(APPLE)
    # A universal macOS build (arm64 and x86_64 at once) has no single answer for
    # test_big_endian; both are little-endian.
    set(_zc_big_endian FALSE)
else()
    include(TestBigEndian)
    test_big_endian(_zc_big_endian)
endif()
if(_zc_big_endian)
    set(_zc_gme_endian BLARGG_BIG_ENDIAN=1)
else()
    set(_zc_gme_endian BLARGG_LITTLE_ENDIAN=1)
endif()
# PUBLIC: the emulator headers depend on them (Vgm_Emu's layout follows the
# YM2612 core), so code using the C++ classes must see the same ones.
target_compile_definitions(zc_gme PUBLIC VGM_YM2612_GENS ${_zc_gme_endian})
target_compile_options(zc_gme PRIVATE ${_zc_audio_quiet})
# GME needs C++11 (its own build uses 11); the top-level default is C++98.
set_target_properties(zc_gme PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
if(NINTENDO_SWITCH)
    # Uses time_t without <time.h>; glibc's other headers happen to bring it in.
    set_property(SOURCE ${ZC_THIRD_PARTY}/gme/gme/Hes_Emu.cpp APPEND PROPERTY COMPILE_OPTIONS -include time.h)
endif()

# --- DUMB ---------------------------------------------------------------------
file(GLOB _zc_dumb_sources CONFIGURE_DEPENDS
    ${ZC_THIRD_PARTY}/dumb/src/core/*.c
    ${ZC_THIRD_PARTY}/dumb/src/helpers/*.c
    ${ZC_THIRD_PARTY}/dumb/src/it/*.c)
add_library(zc_dumb STATIC ${_zc_dumb_sources})
target_include_directories(zc_dumb PUBLIC ${ZC_THIRD_PARTY}/dumb/include)
if(MSVC)
    target_compile_definitions(zc_dumb PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_compile_options(zc_dumb PRIVATE ${_zc_audio_quiet})

# --- TinySoundFont ------------------------------------------------------------
add_library(zc_tsf INTERFACE)
target_include_directories(zc_tsf INTERFACE ${ZC_THIRD_PARTY}/tinysoundfont)

# Built only when something links them (zplayer), not with every default build.
set_target_properties(zc_stb_vorbis zc_gme zc_dumb PROPERTIES EXCLUDE_FROM_ALL TRUE)
