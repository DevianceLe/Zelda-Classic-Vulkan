# zplayer port: the Allegro 4.4 compatible layer (zcport) and the SDL3 +
# Vulkan platform for the player. Included from the top-level CMakeLists.txt
# when ZC_BUILD_ZPLAYER is ON.
#
# Targets:
#   zcport                 STATIC  src/port/al/*.cpp        (no SDL)
#   zcport_sdl             STATIC  src/port/platform/*.cpp  (ZC_PORT_SDL only)
#   zc_vulkan              STATIC  Vulkan presentation      (ZC_PORT_SDL only)
#   vk_present_test        EXE     presentation check       (ZC_PORT_SDL only)
#   zplayer_compile_check  OBJECT  every player source compiled against
#                                  src/port/include with ZC_PORT defined
#   zplayer                EXE     the player: zplayer_compile_check's objects
#                                  + src/port/entry + zcport_sdl + zcport
#                                  (ZC_PORT_SDL only), <build>/<config>/zplayer.exe
#   zcmusic_smoke          EXE     music check: zcmusic on zcport's streams to
#                                  WAV files (built on request)
#   conf_*_ref / conf_*_port       conformance tests (tests/conformance)
#
# Nintendo Switch homebrew (NINTENDO_SWITCH, devkitPro's Switch.cmake): no SDL3
# or Vulkan there. zplayer is the game's objects + zcport + the libnx platform
# (platform/platform_switch.cpp, entry/zplayer_switch.cpp), packed into
# zplayer.nro (target zplayer_nro).

cmake_minimum_required(VERSION 3.16)

if(NINTENDO_SWITCH)
    set(_zc_port_sdl_default OFF)
else()
    set(_zc_port_sdl_default ON)
endif()
option(ZC_PORT_SDL "Build the SDL3 platform layer (zcport_sdl, zc_vulkan)" ${_zc_port_sdl_default})

if(NOT DEFINED ZC_THIRD_PARTY)
    set(ZC_THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)
endif()
set(ZCPORT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# glibc declares C23's fadd, fsub and friends, which clash with Allegro's old names
# for its fixed-point functions. Nothing here uses those names.
if(UNIX AND NOT ANDROID)
    add_compile_definitions(ALLEGRO_NO_FIX_ALIASES)
endif()

# Match the rest of the project: static C runtime (/MT, /MTd).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The top-level CMakeLists forces /SUBSYSTEM:WINDOWS on every executable. That
# breaks CMake's feature checks here (their test programs have no WinMain), so
# SDL3 would wrongly think XInput, DirectInput etc. are missing. Executables in
# this directory choose their subsystem with WIN32_EXECUTABLE instead.
if(MSVC)
    string(REPLACE "/SUBSYSTEM:WINDOWS" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
endif()

# The top-level directory puts allegro/include (and the add-on library
# headers) on every target's include path. Nothing in the port may see the
# real Allegro headers: the replacement headers in src/port/include must win.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

# Music decoders (OGG, MP3, GME, DUMB) and the MIDI synth, all from third_party/.
include(${CMAKE_CURRENT_LIST_DIR}/audio_libs.cmake)

# --- zcport: the Allegro compatible library (no SDL) -------------------------
file(GLOB ZCPORT_SOURCES CONFIGURE_DEPENDS ${ZCPORT_DIR}/al/*.cpp)
file(GLOB ZCPORT_PRIVATE_HEADERS CONFIGURE_DEPENDS ${ZCPORT_DIR}/al/*.h)
file(GLOB_RECURSE ZCPORT_PUBLIC_HEADERS CONFIGURE_DEPENDS ${ZCPORT_DIR}/include/*.h)

add_library(zcport STATIC ${ZCPORT_SOURCES} ${ZCPORT_PRIVATE_HEADERS} ${ZCPORT_PUBLIC_HEADERS})
target_include_directories(zcport PUBLIC ${ZCPORT_DIR}/include)
target_compile_definitions(zcport PUBLIC ZC_PORT=1)
set_target_properties(zcport PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # C4996: the POSIX names (stricmp, ...) the Allegro API itself uses.
    target_compile_options(zcport PRIVATE /W3 /wd4996 /utf-8)
endif()
if(WIN32)
    target_link_libraries(zcport PUBLIC winmm)
endif()

# --- SDL3 platform -------------------------------------------------------------
if(ZC_PORT_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    add_subdirectory(${ZC_THIRD_PARTY}/SDL3 ${CMAKE_BINARY_DIR}/third_party/SDL3 EXCLUDE_FROM_ALL)

    # Vulkan presentation
    # Dear ImGui (zplayer's menus and dialogs, src/port/ui): core + SDL3 input +
    # Vulkan renderer, the renderer through volk like vk_present.cpp.
    set(ZC_IMGUI_DIR ${ZC_THIRD_PARTY}/imgui)
    add_library(zc_imgui STATIC
        ${ZC_IMGUI_DIR}/imgui.cpp
        ${ZC_IMGUI_DIR}/imgui_draw.cpp
        ${ZC_IMGUI_DIR}/imgui_tables.cpp
        ${ZC_IMGUI_DIR}/imgui_widgets.cpp
        ${ZC_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${ZC_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(zc_imgui PUBLIC
        ${ZC_IMGUI_DIR}
        ${ZC_IMGUI_DIR}/backends
        ${ZC_THIRD_PARTY}/Vulkan-Headers/include
        ${ZC_THIRD_PARTY}/volk
    )
    target_compile_definitions(zc_imgui PUBLIC VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK)
    target_link_libraries(zc_imgui PUBLIC SDL3::SDL3-static)
    set_target_properties(zc_imgui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(zc_imgui PRIVATE /W3 /utf-8)
    endif()

    add_library(zc_vulkan STATIC
        vk_present.cpp
        vk_present.h
        ${ZC_THIRD_PARTY}/volk/volk.c
    )
    target_include_directories(zc_vulkan PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ZC_THIRD_PARTY}/Vulkan-Headers/include
        ${ZC_THIRD_PARTY}/volk
    )
    # volk loads every Vulkan function at runtime, so nothing links vulkan-1.lib.
    target_compile_definitions(zc_vulkan PUBLIC VK_NO_PROTOTYPES)
    target_link_libraries(zc_vulkan PUBLIC SDL3::SDL3-static zc_imgui)
    set_target_properties(zc_vulkan PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    # Stand-alone check of the presentation path
    add_executable(vk_present_test vk_present_test.cpp)
    target_link_libraries(vk_present_test PRIVATE zc_vulkan)
    set_target_properties(vk_present_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    # zcport_sdl: the platform layer (fills the zcport/platform.h hook table)
    file(GLOB ZCPORT_SDL_SOURCES CONFIGURE_DEPENDS ${ZCPORT_DIR}/platform/*.cpp ${ZCPORT_DIR}/ui/*.cpp)
    list(FILTER ZCPORT_SDL_SOURCES EXCLUDE REGEX "_switch\\.cpp$")
    file(GLOB ZCPORT_SDL_HEADERS CONFIGURE_DEPENDS ${ZCPORT_DIR}/platform/*.h ${ZCPORT_DIR}/ui/*.h)
    if(NOT ZCPORT_SDL_SOURCES)
        # No platform module yet: keep the target valid.
        set(ZCPORT_SDL_EMPTY ${CMAKE_CURRENT_BINARY_DIR}/zcport_sdl_empty.cpp)
        if(NOT EXISTS ${ZCPORT_SDL_EMPTY})
            file(WRITE ${ZCPORT_SDL_EMPTY} "// placeholder until src/port/platform has sources\n")
        endif()
        set(ZCPORT_SDL_SOURCES ${ZCPORT_SDL_EMPTY})
    endif()
    add_library(zcport_sdl STATIC ${ZCPORT_SDL_SOURCES} ${ZCPORT_SDL_HEADERS})
    target_link_libraries(zcport_sdl PUBLIC zcport SDL3::SDL3-static zc_vulkan zc_tsf)
    target_include_directories(zcport_sdl PRIVATE ${ZCPORT_DIR}/ui)
    if(WIN32)
        # MIDI output through the MIDI mapper (platform/midi_winmm.cpp)
        target_link_libraries(zcport_sdl PUBLIC winmm)
    endif()
    set_target_properties(zcport_sdl PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(zcport_sdl PRIVATE /W3 /wd4996 /utf-8)
    endif()
    # platform_sdl.cpp registers the platform from a static constructor;
    # keep that object in every program linking zcport_sdl (nothing else
    # references it by name).
    if(ZCPORT_SDL_SOURCES MATCHES "platform_sdl\\.cpp")
        if(MSVC)
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                target_link_options(zcport_sdl INTERFACE "/INCLUDE:_zcport_sdl_link_anchor")
            else()
                target_link_options(zcport_sdl INTERFACE "/INCLUDE:zcport_sdl_link_anchor")
            endif()
        elseif(APPLE)
            # Apple's linker: -u, with the C symbol's leading underscore
            target_link_options(zcport_sdl INTERFACE "-Wl,-u,_zcport_sdl_link_anchor")
        else()
            target_link_options(zcport_sdl INTERFACE "-Wl,--undefined=zcport_sdl_link_anchor")
        endif()
    endif()
endif()

# Switch: Dear ImGui without the SDL3 and Vulkan backends; its renderer is
# platform/gles_present_switch.cpp.
if(NINTENDO_SWITCH)
    set(ZC_IMGUI_DIR ${ZC_THIRD_PARTY}/imgui)
    add_library(zc_imgui STATIC
        ${ZC_IMGUI_DIR}/imgui.cpp
        ${ZC_IMGUI_DIR}/imgui_draw.cpp
        ${ZC_IMGUI_DIR}/imgui_tables.cpp
        ${ZC_IMGUI_DIR}/imgui_widgets.cpp
    )
    target_include_directories(zc_imgui PUBLIC ${ZC_IMGUI_DIR})
    # (fork/waitpid for opening links: not on Switch)
    target_compile_definitions(zc_imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
    set_target_properties(zc_imgui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endif()

# The unified engine (src/port/engine) uses only the libraries above.
if(ZC_PORT_LAYER_ONLY)
    return()
endif()

# --- compile check of the player sources against the replacement headers ------
# Same source lists as the "zelda" target (modules/zelda/*.txt) plus the
# zcmusic sources, minus the files that are replaced by src/port/game.
set(ZPLAYER_GAME_SOURCES
    ${ZELDA_CORE_SOURCES}
    ${ZELDA_GUI_SOURCES}
    ${ZELDA_SPRITE_SOURCES}
    ${ZELDA_SUBSCREEN_SOURCES}
    ${ZELDA_SCRIPTING_SOURCES}
    ${ZCSOUNDSOURCES}
    # load_gif()/save_gif() (registered as the "GIF" bitmap type, used for
    # .gif screenshots): zelda gets them from the al_gif add-on library,
    # zplayer compiles the game's own GIF code that ZQuest uses.
    src/load_gif.cpp
    src/save_gif.cpp
)
# zcmusic.cpp needs the Allegro add-on music libraries (DUMB, alogg, almp3,
# GME); src/port/game/zcmusic_port.cpp replaces it, on the decoders under
# third_party/ (src/port/game/zcmusic_decoders.cpp).
# Replaced in zplayer: jwin.cpp (the old widget library) by Dear ImGui
# (src/port/game/jwin_port.cpp keeps the jwin API the player uses, the
# dialog host in src/port/game/ui_dialogs_*.cpp shows the dialogs),
# jwinfsel.cpp (jwin's file selector) by the ImGui file browser
# src/port/game/ui_filesel*.cpp (same functions, same results). The
# editbox files are only used by ZQuest's dialogs.
set(ZPLAYER_REPLACED_SOURCES
    src/zcmusic.cpp
    src/jwin.cpp
    src/jwinfsel.cpp
    src/editbox.cpp
    src/EditboxModel.cpp
    src/EditboxView.cpp
)
list(REMOVE_DUPLICATES ZPLAYER_GAME_SOURCES)
list(REMOVE_ITEM ZPLAYER_GAME_SOURCES ${ZPLAYER_REPLACED_SOURCES})

set(ZPLAYER_SOURCES)
foreach(src ${ZPLAYER_GAME_SOURCES})
    if(IS_ABSOLUTE ${src})
        list(APPEND ZPLAYER_SOURCES ${src})
    else()
        list(APPEND ZPLAYER_SOURCES ${CMAKE_SOURCE_DIR}/${src})
    endif()
endforeach()
file(GLOB ZPLAYER_PORT_GAME_SOURCES CONFIGURE_DEPENDS ${ZCPORT_DIR}/game/*.cpp)
# The game-side ImGui code (src/port/game/ui_*.cpp: file selector, message
# boxes) needs Dear ImGui, which comes with the SDL3 platform.
if(NOT ZC_PORT_SDL AND NOT NINTENDO_SWITCH)
    list(FILTER ZPLAYER_PORT_GAME_SOURCES EXCLUDE REGEX "/ui_[^/]*\\.cpp$")
endif()
# Switch: no on-screen gamepad (its key source lives in platform_touch.cpp,
# which needs SDL).
if(NINTENDO_SWITCH)
    list(FILTER ZPLAYER_PORT_GAME_SOURCES EXCLUDE REGEX "/touch_keys\\.cpp$")
endif()
list(APPEND ZPLAYER_SOURCES ${ZPLAYER_PORT_GAME_SOURCES})
# is_only_instance(): src/win32.cpp on Windows (part of the core sources),
# src/port/game/single_instance_port.cpp on Android, the Allegro build's
# Unix version on the other systems.
if(UNIX AND NOT ANDROID)
    list(APPEND ZPLAYER_SOURCES ${CMAKE_SOURCE_DIR}/src/single_instance_unix.cpp)
endif()
set(ZPLAYER_SOURCES ${ZPLAYER_SOURCES} PARENT_SCOPE)

# The zplayer executable links these objects (compiled once, see below).
add_library(zplayer_compile_check OBJECT EXCLUDE_FROM_ALL ${ZPLAYER_SOURCES})
# The music decoders: their include paths for src/port/game (the libraries
# themselves are linked by zplayer, which takes these objects).
set(ZPLAYER_MUSIC_LIBS zc_stb_vorbis zc_minimp3 zc_gme zc_dumb)
target_link_libraries(zplayer_compile_check PRIVATE zcport ${ZPLAYER_MUSIC_LIBS})
if(TARGET zc_imgui)
    # src/port/game/ui_*.cpp (include paths only; zplayer links ImGui
    # through zcport_sdl).
    target_link_libraries(zplayer_compile_check PRIVATE zc_imgui)
endif()
target_include_directories(zplayer_compile_check PRIVATE ${CMAKE_SOURCE_DIR}/src)
# The game's main() becomes zcport_game_main() (allegro/internal/alconfig.h);
# src/port/entry/zplayer_main.cpp is the real entry point.
target_compile_definitions(zplayer_compile_check PRIVATE ZCPORT_MAGIC_MAIN ZC_PLAYER_BUILD)
# 64-bit Linux/Android (LP64): the game sources keep 32-bit 'long' and
# Microsoft's rand() through a forced include (zcport/lp64.h); never on
# Windows, never for zcport, the platform layer or third-party code. ARM
# compilers default to unsigned 'char'; the game expects signed (as on x86).
if(NOT WIN32 AND NOT MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_zc_lp64_sources ${ZPLAYER_SOURCES})
    list(REMOVE_ITEM _zc_lp64_sources ${ZCPORT_DIR}/game/zcmusic_decoders.cpp)
    set_property(SOURCE ${_zc_lp64_sources} APPEND PROPERTY COMPILE_OPTIONS
        -include "${ZCPORT_DIR}/include/zcport/lp64.h")
endif()
if(NOT MSVC)
    target_compile_options(zplayer_compile_check PRIVATE -fsigned-char)
endif()
if((UNIX AND NOT ANDROID) OR NINTENDO_SWITCH)
    # The game's C++98 tables were written before narrowing became an error.
    target_compile_options(zplayer_compile_check PRIVATE -Wno-narrowing)
endif()
if(NOT MSVC)
    # The Game_Music_Emu headers need C++11; the game itself is built as C++98
    # with GCC/Clang (MSVC uses its own default for everything).
    set_source_files_properties(${ZCPORT_DIR}/game/zcmusic_decoders.cpp PROPERTIES COMPILE_OPTIONS "-std=gnu++11")
    # zplayer's ImGui UI (src/port/game/ui_*.cpp) needs C++11 as well.
    file(GLOB _zc_ui_game_sources ${ZCPORT_DIR}/game/ui_*.cpp)
    set_property(SOURCE ${_zc_ui_game_sources} APPEND PROPERTY COMPILE_OPTIONS -std=gnu++11)
endif()
if(MSVC)
    # Same warning level as the zelda target (the default), keep the noise of
    # the old code base out of the log.
    target_compile_options(zplayer_compile_check PRIVATE /wd4996 /wd4244 /wd4267 /wd4305 /wd4018)
endif()

# --- zcmusic_smoke: the music path without the game (built on request) -------
# Plays files through src/port/game/zcmusic_port.cpp on zcport's audio streams
# and writes what the streams receive to WAV files (src/port/tools/zcmusic_smoke.cpp).
add_executable(zcmusic_smoke EXCLUDE_FROM_ALL
    tools/zcmusic_smoke.cpp
    game/zcmusic_port.cpp
    game/zcmusic_decoders.cpp
    ${CMAKE_SOURCE_DIR}/src/alleg_compat.cpp
)
target_include_directories(zcmusic_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src ${ZCPORT_DIR}/game)
target_link_libraries(zcmusic_smoke PRIVATE zcport ${ZPLAYER_MUSIC_LIBS})
set_target_properties(zcmusic_smoke PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # As the game objects: zcmusic returns TRUE/FALSE from bool functions.
    target_compile_options(zcmusic_smoke PRIVATE /wd4996 /wd4305)
endif()

# --- zplayer: the player executable (SDL3 + Vulkan) ---------------------------
# Output: <build>/<config>/zplayer.exe, next to the Allegro zelda.exe. The
# player opens its data relative to the working directory, like zelda.exe:
# set ZPLAYER_DATA_DIR to a folder holding the game data (the reference
# build's output, for instance) and the data files are copied next to
# zplayer.exe after every build; then start zplayer.exe from that folder
# (the Visual Studio debugger does). See docs/zplayer-port.md.
if(ZC_PORT_SDL)
    set(_zplayer_default_data "")
    if(EXISTS ${CMAKE_SOURCE_DIR}/build/Release/zelda.dat)
        set(_zplayer_default_data ${CMAKE_SOURCE_DIR}/build/Release)
    endif()
    set(ZPLAYER_DATA_DIR "${_zplayer_default_data}" CACHE PATH
        "Folder with the player's data (zelda.dat, fonts.dat, sfx.dat, qst.dat, *.qst, *.nsf), copied next to zplayer.exe after each build; empty = no copy")

    file(GLOB ZPLAYER_ENTRY_SOURCES CONFIGURE_DEPENDS ${ZCPORT_DIR}/entry/*.cpp)
    list(FILTER ZPLAYER_ENTRY_SOURCES EXCLUDE REGEX "_switch\\.cpp$")
    set(ZPLAYER_RESOURCES)
    if(MSVC)
        # Same icon resource as zelda.exe ("allegro_icon").
        set(ZPLAYER_RESOURCES ${CMAKE_SOURCE_DIR}/zc_icon.rc)
    endif()

    # Until every zcport module is merged the link fails (docs/zplayer-link-gaps.md),
    # so zplayer is built on request only (--target zplayer) unless this is ON.
    # A zplayer-only configuration (ZC_ZPLAYER_ONLY, 64-bit builds) exists
    # for zplayer, so there it is part of ALL by default.
    if(ZC_ZPLAYER_ONLY)
        set(_zplayer_in_all_default ON)
    else()
        set(_zplayer_in_all_default OFF)
    endif()
    option(ZPLAYER_IN_ALL "Build zplayer with the default (ALL) target; turn on once it links" ${_zplayer_in_all_default})

    if(ANDROID)
        # libzplayer.so, loaded by the Java activity (android/); SDL3 is linked
        # in statically and SDL_main comes from src/port/entry/zplayer_main.cpp.
        add_library(zplayer SHARED ${ZPLAYER_ENTRY_SOURCES})
        target_link_libraries(zplayer PRIVATE android log)
    else()
        # (autotest: the no-focus test driver, inert unless ZQ_UI_SCRIPT is set)
        add_executable(zplayer WIN32 ${ZPLAYER_ENTRY_SOURCES} ${ZPLAYER_RESOURCES} ${ZCPORT_DIR}/zqeditor/autotest.cpp)
    endif()
    if(NOT ZPLAYER_IN_ALL)
        set_target_properties(zplayer PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    # The game itself: the objects of zplayer_compile_check (same sources and
    # flags, so they are compiled once for both targets).
    target_sources(zplayer PRIVATE $<TARGET_OBJECTS:zplayer_compile_check>)
    add_dependencies(zplayer zplayer_compile_check)
    target_link_libraries(zplayer PRIVATE zcport_sdl zcport ${ZPLAYER_MUSIC_LIBS})
    if(UNIX AND NOT ANDROID AND NOT APPLE)
        # One download for every distribution: don't depend on the system's C++ runtime.
        # (macOS has one C++ runtime, part of the system, and no such options.)
        target_link_options(zplayer PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    set_target_properties(zplayer PROPERTIES
        OUTPUT_NAME zplayer
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)"
    )
    if(MSVC)
        target_compile_options(zplayer PRIVATE /W3 /utf-8)
    endif()

    if(ZPLAYER_DATA_DIR)
        set(ZPLAYER_DATA_FILES)
        foreach(_pattern zelda.dat fonts.dat sfx.dat qst.dat *.qst *.nsf)
            file(GLOB _found "${ZPLAYER_DATA_DIR}/${_pattern}")
            list(APPEND ZPLAYER_DATA_FILES ${_found})
        endforeach()
        if(ZPLAYER_DATA_FILES)
            add_custom_command(TARGET zplayer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZPLAYER_DATA_FILES} $<TARGET_FILE_DIR:zplayer>
                COMMENT "Copying the player's data next to zplayer"
                VERBATIM)
        else()
            message(WARNING "ZPLAYER_DATA_DIR (${ZPLAYER_DATA_DIR}) holds none of the player's data files")
        endif()
    endif()
endif()

# --- zplayer on Nintendo Switch homebrew (libnx) -------------------------------
if(NINTENDO_SWITCH)
    add_executable(zplayer
        ${ZCPORT_DIR}/entry/zplayer_switch.cpp
        ${ZCPORT_DIR}/platform/platform_switch.cpp
        ${ZCPORT_DIR}/platform/gles_present_switch.cpp
        ${ZCPORT_DIR}/platform/midi_tsf.cpp
        ${ZCPORT_DIR}/ui/ui_core.cpp
    )
    target_sources(zplayer PRIVATE $<TARGET_OBJECTS:zplayer_compile_check>)
    add_dependencies(zplayer zplayer_compile_check)
    target_include_directories(zplayer PRIVATE ${ZCPORT_DIR}/ui ${ZCPORT_DIR}/platform ${DEVKITPRO}/portlibs/switch/include)
    # switch-mesa: EGL and OpenGL ES.
    target_link_libraries(zplayer PRIVATE zcport zc_tsf zc_imgui ${ZPLAYER_MUSIC_LIBS} GLESv2 EGL glapi drm_nouveau)
    set_target_properties(zplayer PROPERTIES
        OUTPUT_NAME zplayer
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    # App title and version for the homebrew menu, from the window title.
    file(STRINGS ${ZCPORT_DIR}/include/zcport/entry.h _zplayer_title_line
         REGEX "define ZPLAYER_WINDOW_TITLE")
    string(REGEX MATCH "ZC Vulkan ([0-9A-Za-z.-]+)" _zplayer_title_match "${_zplayer_title_line}")
    nx_generate_nacp(
        OUTPUT ${CMAKE_BINARY_DIR}/zplayer.nacp
        NAME "ZC Vulkan"
        AUTHOR "Deviance"
        VERSION "${CMAKE_MATCH_1}"
    )
    nx_create_nro(zplayer NACP ${CMAKE_BINARY_DIR}/zplayer.nacp OUTPUT ${CMAKE_BINARY_DIR}/zplayer.nro)
endif()

# Release builds keep full debug info and frame pointers, so crash reports
# (src/port/entry/crash_win32.cpp) name functions and lines.
if(MSVC)
    foreach(_zc_target zcport zcport_sdl zplayer_compile_check zplayer)
        if(TARGET ${_zc_target})
            target_compile_options(${_zc_target} PRIVATE "$<$<CONFIG:Release>:/Zi>" "$<$<CONFIG:Release>:/Oy->")
        endif()
    endforeach()
endif()

# --- zqeditor: ZQuest on the port (Windows) ------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/zqeditor.cmake)

# --- conformance harness -------------------------------------------------------
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/conformance/CMakeLists.txt AND NOT NINTENDO_SWITCH)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests/conformance ${CMAKE_BINARY_DIR}/conformance)
endif()
