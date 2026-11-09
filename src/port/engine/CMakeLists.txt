# The unified engine: upstream ZQuestClassic (engine/) on the zcport layer.
# A standalone 64-bit project, separate from the 32-bit 2.53 build:
#   cmake -S src/port/engine -B build-engine -G "Visual Studio 17 2022" -A x64
cmake_minimum_required(VERSION 3.20)
project(zc_engine C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The unified engine is 64-bit: configure with -A x64")
endif()

get_filename_component(ZC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
set(ENGINE ${ZC_ROOT}/engine)
set(ENGINE_SRC ${ENGINE}/src)
set(ENGINE_TP ${ENGINE}/third_party)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The zcport libraries, shared with the 2.53 engine.
set(ZC_THIRD_PARTY ${ZC_ROOT}/third_party)
set(ZC_PORT_LAYER_ONLY ON)
add_subdirectory(.. zcport)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(MSVC)
    add_compile_options(/MP /utf-8 /wd4996 /bigobj)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
add_compile_definitions(ALLEGRO_NO_MAGIC_MAIN ALLEGRO_UNSTABLE ALLEGRO_LEGACY_MSVC)

# --- third party libraries upstream vendors ------------------------------------
add_library(fmt STATIC ${ENGINE_TP}/fmt/src/format.cc)
target_include_directories(fmt SYSTEM PUBLIC ${ENGINE_TP}/fmt/include)

add_library(json INTERFACE)
target_include_directories(json SYSTEM INTERFACE ${ENGINE_TP}/json/include)
add_library(nlohmann_json::nlohmann_json ALIAS json)

# The from-scratch Allegro 5 headers and the shims for libraries left out.
set(ENGINE_PORT_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- base ----------------------------------------------------------------------
add_library(base STATIC
    ${ENGINE_SRC}/base/about.cpp
    ${ENGINE_SRC}/base/check.cpp
    ${ENGINE_SRC}/base/containers.cpp
    ${ENGINE_SRC}/base/crash_backtrace.cpp
    ${ENGINE_SRC}/base/general.cpp
    ${ENGINE_SRC}/base/md5.cpp
    ${ENGINE_SRC}/base/process_management.cpp
    ${ENGINE_SRC}/base/random.cpp
    ${ENGINE_SRC}/base/sin1.cpp
    ${ENGINE_SRC}/base/util.cpp
    ${ENGINE_SRC}/base/version.cpp
    ${ENGINE_SRC}/base/win_encoding.cpp
    ${ENGINE_SRC}/base/zapp.cpp
    ${ENGINE_SRC}/base/zc_math.cpp
    ${ENGINE_SRC}/base/zfix.cpp
)
target_include_directories(base PUBLIC
    ${ENGINE_PORT_INCLUDE}
    ${ENGINE_SRC}
    ${ENGINE}/include
    ${ENGINE_TP}/nonstd/include
)
target_link_libraries(base PUBLIC fmt nlohmann_json::nlohmann_json zcport)

# --- zconsolelogger, scc, zasm -------------------------------------------------
add_library(zconsolelogger STATIC ${ENGINE_SRC}/zconsole/ConsoleLogger.cpp)
target_link_libraries(zconsolelogger PUBLIC base)

add_library(scc STATIC ${ENGINE_SRC}/components/scc/scc.cpp)
target_link_libraries(scc PUBLIC base)

add_library(zasm STATIC
    ${ENGINE_SRC}/components/zasm/debug_data.cpp
    ${ENGINE_SRC}/components/zasm/eval.cpp
    ${ENGINE_SRC}/components/zasm/inst_var_remap.cpp
    ${ENGINE_SRC}/components/zasm/serialize.cpp
    ${ENGINE_SRC}/components/zasm/table.cpp
)
target_link_libraries(zasm PUBLIC base)

# --- zalleg (upstream's own helpers, built here on zcport) ----------------------
add_library(zalleg STATIC
    ${ENGINE_SRC}/zalleg/colors.cpp
    ${ENGINE_SRC}/zalleg/files.cpp
    ${ENGINE_SRC}/zalleg/gui.cpp
    ${ENGINE_SRC}/zalleg/new_menu.cpp
    ${ENGINE_SRC}/zalleg/packfile.cpp
    ${ENGINE_SRC}/zalleg/pal_tables.cpp
    ${ENGINE_SRC}/zalleg/render.cpp
    ${ENGINE_SRC}/zalleg/render_tree.cpp
    ${ENGINE_SRC}/zalleg/zalleg.cpp
    ${ENGINE_SRC}/zalleg/zqdialog.cpp
    ${ENGINE_SRC}/zalleg/zsys.cpp
)
target_link_libraries(zalleg PUBLIC base zconsolelogger)

# --- core: quest data and .qst reading ------------------------------------------
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${ENGINE_SRC}/core/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "_test\\.cpp$")

set(QR_NAMES_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/qr_names.cpp)
add_custom_command(
    OUTPUT ${QR_NAMES_CPP}
    COMMAND ${Python3_EXECUTABLE} ${ENGINE}/scripts/gen_qr_names.py ${ENGINE_SRC}/core/qrs.h ${QR_NAMES_CPP}
    DEPENDS ${ENGINE}/scripts/gen_qr_names.py ${ENGINE_SRC}/core/qrs.h
    COMMENT "Generating quest rule name table"
    VERBATIM
)
add_library(core STATIC ${CORE_SOURCES} ${QR_NAMES_CPP})
target_link_libraries(core PUBLIC zalleg scc zasm)

# --- widgets, workers, sound ----------------------------------------------------
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS ${ENGINE_SRC}/gui/*.cpp)
list(FILTER GUI_SOURCES EXCLUDE REGEX "_test\\.cpp$")
add_library(zcgui STATIC ${GUI_SOURCES})
target_link_libraries(zcgui PUBLIC core)

add_library(worker_pool STATIC ${ENGINE_SRC}/components/worker_pool/worker_pool.cpp)
target_link_libraries(worker_pool PUBLIC base)

add_library(zcsound STATIC
    ${ENGINE_SRC}/components/sound/midi.cpp
    ${ENGINE_SRC}/components/sound/zcmixer.cpp
    ${ENGINE_SRC}/components/sound/zcmusic.cpp
)
target_include_directories(zcsound PUBLIC ${ENGINE}/include ${ZC_THIRD_PARTY}/gme/gme)
target_link_libraries(zcsound PUBLIC base zc_gme zc_dumb)
target_compile_definitions(zcsound PUBLIC SOUND_LIBS_BUILT_FROM_SOURCE)
# zcmusic.cpp and zelda.cpp each define the same old MSVC runtime shim.
target_compile_definitions(zcsound PRIVATE _iob=zcsound_unused_iob __iob_func=zcsound_unused_iob_func)

# --- the player -----------------------------------------------------------------
# Upstream's script JIT needs asmjit; the interpreter runs scripts until Phase 3.
target_compile_definitions(base PUBLIC ZC_JIT_BACKEND=0)

# Upstream's own source lists for the player.
foreach(list_file ZeldaCore ZeldaGUI ZeldaSprite ZeldaSubscreen)
    include(${CMAKE_CURRENT_SOURCE_DIR}/modules/${list_file}.txt)
endforeach()
set(ZC_SOURCES ${ZELDA_CORE_SOURCES} ${ZELDA_GUI_SOURCES} ${ZELDA_SPRITE_SOURCES} ${ZELDA_SUBSCREEN_SOURCES})
list(TRANSFORM ZC_SOURCES PREPEND "${ENGINE}/")
list(APPEND ZC_SOURCES ${ENGINE_SRC}/zc/scripting/jit/jit_none.cpp)

add_library(imgui_color_text_edit STATIC ${ENGINE_TP}/ImGuiColorTextEdit/src/TextEditor.cpp)
target_include_directories(imgui_color_text_edit PUBLIC ${ENGINE_TP}/ImGuiColorTextEdit/src)
target_link_libraries(imgui_color_text_edit PRIVATE zc_imgui)

add_library(zplayer_objects OBJECT ${ZC_SOURCES})
target_compile_definitions(zplayer_objects PRIVATE IS_PLAYER ZCPORT_MAGIC_MAIN)
target_include_directories(zplayer_objects PRIVATE
    ${ENGINE}/include/xxhash
    # Headers under zc/scripting/types reach src/ with "../../x.h" from here.
    ${ENGINE_SRC}/zc/scripting
    ${ENGINE_TP}/ImGuiColorTextEdit/src
    ${ZC_THIRD_PARTY}/imgui
    ${ZC_THIRD_PARTY}/imgui/backends
)
target_link_libraries(zplayer_objects PUBLIC core zcsound zconsolelogger zcgui worker_pool)

# --- the ImGui dialog host, shared with the 2.53 engine ---------------------------
add_library(engine_dialogs OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/jwin_port.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_dialogs_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_dialogs_layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_filesel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_filesel_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_fs_os.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_msgbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/dialog_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/system_menu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../game/ui_video.cpp
)
target_include_directories(engine_dialogs PRIVATE ${ENGINE_SRC}/gui ${ENGINE_SRC}/core ${CMAKE_CURRENT_SOURCE_DIR}/../ui ${CMAKE_CURRENT_SOURCE_DIR}/../game)
target_compile_definitions(engine_dialogs PRIVATE ZC_ENGINE)
target_link_libraries(engine_dialogs PUBLIC core zcgui zcport_sdl)

add_executable(zplayer $<TARGET_OBJECTS:zplayer_objects> $<TARGET_OBJECTS:engine_dialogs>
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/a5_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/a5_audio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/a5_gui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/a5_win.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/a5/dialog_placeholders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../zqeditor/image_wic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../zqeditor/autotest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../zqeditor/image_loaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../entry/zplayer_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../entry/crash_win32.cpp
)
set_target_properties(zplayer PROPERTIES WIN32_EXECUTABLE TRUE)
target_include_directories(zplayer PRIVATE ${ENGINE_SRC} ${ZC_THIRD_PARTY}/imgui ${CMAKE_CURRENT_SOURCE_DIR}/../zqeditor)
target_link_libraries(zplayer PRIVATE core zcsound zconsolelogger zcgui worker_pool imgui_color_text_edit zcport_sdl zc_stb_vorbis zc_minimp3 winmm windowscodecs ole32)
