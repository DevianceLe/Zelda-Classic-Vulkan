# zqeditor: ZQuest without Allegro (SDL3 + Vulkan): Windows, Linux and macOS.
#
# Included from src/port/CMakeLists.txt, whose directory hides the real Allegro
# headers. The sources are the same as the Allegro "zquest" target's, which
# stays as the reference; the music code comes from src/port/game, as in
# zplayer. The ZScript parser is generated here too: the top-level directory's
# flex and bison steps can only feed targets of that directory.

# It uses none of the prebuilt Allegro libraries, so it builds in a player-only (64-bit)
# configuration too; as before it's built on request (--target zqeditor).
option(ZC_BUILD_ZQEDITOR "Configure zqeditor, ZQuest on SDL3 and Vulkan" ON)
if(ZC_PORT_SDL AND ZC_BUILD_ZQEDITOR AND NOT ANDROID)

# The top-level directory looks for flex and bison only when it configures the Allegro
# targets.
if(NOT BISON_FOUND)
	find_package(BISON QUIET)
endif()
if(NOT FLEX_FOUND)
	find_package(FLEX QUIET)
endif()
if(MSVC)
	set(FLEXFLAGS "--wincompat")
endif()

set(ZQEDITOR_RELATIVE_SOURCES
	${ZQUEST_CORE_SOURCES}
	${ZQUEST_GUI_SOURCES}
	${ZQUEST_MISC_SOURCES}
	${ZQUEST_ZSCRIPT_SOURCES}
	${ZCSOUNDSOURCES}
)
if(WIN32)
	list(APPEND ZQEDITOR_RELATIVE_SOURCES zq_icon.rc)
endif()
list(REMOVE_DUPLICATES ZQEDITOR_RELATIVE_SOURCES)
list(REMOVE_ITEM ZQEDITOR_RELATIVE_SOURCES src/zcmusic.cpp src/jwin.cpp src/jwinfsel.cpp)

set(ZQEDITOR_SOURCES
	${ZCPORT_DIR}/game/zcmusic_port.cpp
	${ZCPORT_DIR}/game/zcmusic_decoders.cpp
	${ZCPORT_DIR}/entry/zplayer_main.cpp
	${ZCPORT_DIR}/entry/crash_win32.cpp
	${ZCPORT_DIR}/game/ui_msgbox.cpp
	${ZCPORT_DIR}/game/ui_fs_os.cpp
	${ZCPORT_DIR}/game/ui_filesel.cpp
	${ZCPORT_DIR}/game/ui_filesel_view.cpp
	${ZCPORT_DIR}/game/ui_dialogs_host.cpp
	${ZCPORT_DIR}/game/ui_dialogs_layer.cpp
	${ZCPORT_DIR}/game/ui_video.cpp
	${ZCPORT_DIR}/game/jwin_port.cpp
	${ZCPORT_DIR}/zqeditor/jwin_zq.cpp
	${ZCPORT_DIR}/zqeditor/autotest.cpp
	${ZCPORT_DIR}/zqeditor/image_loaders.cpp
	# 2.55 and 3.0 Nightly quests: the player's section readers, filling ZQuest's own store
	${ZCPORT_DIR}/game/q255_core.cpp
	${ZCPORT_DIR}/game/q255_basic.cpp
	${ZCPORT_DIR}/game/q255_media.cpp
	${ZCPORT_DIR}/game/q255_combos.cpp
	${ZCPORT_DIR}/game/q255_items.cpp
	${ZCPORT_DIR}/game/q255_guys.cpp
	${ZCPORT_DIR}/game/q255_maps.cpp
	${ZCPORT_DIR}/game/q255_init.cpp
	${ZCPORT_DIR}/game/q255_scripts.cpp
	${ZCPORT_DIR}/game/q30_types.cpp
	${ZCPORT_DIR}/game/quest_version.cpp
	${ZCPORT_DIR}/zqeditor/q255_store_zq.cpp
	${ZCPORT_DIR}/zqeditor/zq_sections.cpp
	${ZCPORT_DIR}/zqeditor/zq_writers.cpp
	${ZCPORT_DIR}/zqeditor/zq_builds_old.cpp
	${ZCPORT_DIR}/zqeditor/zq_tests.cpp
	${ZCPORT_DIR}/zqeditor/zq_audit.cpp
	${ZCPORT_DIR}/zqeditor/zq_tile_refs.cpp
	${ZCPORT_DIR}/zqeditor/zq_combo_names.cpp
	${ZCPORT_DIR}/zqeditor/zq_combo_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_tape.cpp
	${ZCPORT_DIR}/zqeditor/zq_tape_misc.cpp
	${ZCPORT_DIR}/zqeditor/zq_tape_strings.cpp
	${ZCPORT_DIR}/zqeditor/zq_tape_subscreens.cpp
	${ZCPORT_DIR}/zqeditor/zq_tape_link.cpp
	${ZCPORT_DIR}/zqeditor/zq_item_names.cpp
	${ZCPORT_DIR}/zqeditor/zq_record_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_rule_names.cpp
	${ZCPORT_DIR}/zqeditor/zq_rules_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_new_quest.cpp
	${ZCPORT_DIR}/zqeditor/zq_builds_30.cpp
	${ZCPORT_DIR}/zqeditor/zq_builds_30_new.cpp
	# the build catalog: every ZC build File > New starts in and Save As saves as (zq_builds.h)
	${ZCPORT_DIR}/zqeditor/zq_builds.cpp
	${ZCPORT_DIR}/zqeditor/zq_builds_255.cpp
	${ZCPORT_DIR}/zqeditor/zq_builds_192.cpp
	# compiling 2.55 / 3.0 quests' ZScript with zscript255 / zscript30
	${ZCPORT_DIR}/zqeditor/zq_compile.cpp
	${ZCPORT_DIR}/zqeditor/zq_compile_dialog.cpp
	${ZCPORT_DIR}/zqeditor/zq_compile_proc.cpp
	${ZCPORT_DIR}/zqeditor/zq_compile_remap.cpp
	# 3.0's scrolling regions, and the combo pools and autocombos of 2.55.17 and 3.0: the store,
	# the editors, the map editor's Pool and Autocombo drawing modes and upstream's patterns
	${ZCPORT_DIR}/zqeditor/zq_cpool.cpp
	${ZCPORT_DIR}/zqeditor/zq_cpool_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_autocombo_draw.cpp
	${ZCPORT_DIR}/zqeditor/zq_engravings.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/autopattern_base.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_basic.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_cakemtn.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_denseforest.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_dormtn.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_dungeoncarve.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_extend.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_fence.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_flatmtn.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_relational.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_replace.cpp
	${ZCPORT_DIR}/zqeditor/autocombo/pattern_tiling.cpp
	${ZCPORT_DIR}/zqeditor/zq_music_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_hero_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_zinfo_editor.cpp
	${ZCPORT_DIR}/zqeditor/zq_exports.cpp
	# the subscreen layout editor, and the fonts it draws newer quests' widgets with
	${ZCPORT_DIR}/zqeditor/zq_subscr_layout.cpp
	${ZCPORT_DIR}/game/q30_fonts.cpp
)
# Image import: the Windows Imaging Component on Windows, SDL elsewhere (PNG, BMP).
if(WIN32)
	list(APPEND ZQEDITOR_SOURCES ${ZCPORT_DIR}/zqeditor/image_wic.cpp)
else()
	list(APPEND ZQEDITOR_SOURCES ${ZCPORT_DIR}/zqeditor/image_sdl.cpp)
endif()
# Section writers for saving quests in the version they were made in (see zq_writers.h):
# one file per area, picked up as they're added.
file(GLOB ZQEDITOR_SECTION_WRITERS CONFIGURE_DEPENDS ${ZCPORT_DIR}/zqeditor/zqw_*.cpp ${ZCPORT_DIR}/zqeditor/zqn_*.cpp)
list(APPEND ZQEDITOR_SOURCES ${ZQEDITOR_SECTION_WRITERS})
foreach(src ${ZQEDITOR_RELATIVE_SOURCES})
	if(IS_ABSOLUTE ${src})
		list(APPEND ZQEDITOR_SOURCES ${src})
	else()
		list(APPEND ZQEDITOR_SOURCES ${CMAKE_SOURCE_DIR}/${src})
	endif()
endforeach()

# (The parser's headers include "y.tab.hpp" by name. A tree that still holds the copies
# the Allegro build generates into src/parser finds those first, so build Linux and macOS
# from a clean checkout: src/parser/.gitignore keeps them out of the repository.)
if(BISON_FOUND AND FLEX_FOUND AND WANT_ZSCRIPT)
	set(ZQEDITOR_PARSER_DIR ${CMAKE_CURRENT_BINARY_DIR}/zqeditor_parser)
	file(MAKE_DIRECTORY ${ZQEDITOR_PARSER_DIR})
	FLEX_TARGET(ZqeditorLexer ${CMAKE_SOURCE_DIR}/src/parser/ffscript.lpp ${ZQEDITOR_PARSER_DIR}/lex.yy.cpp COMPILE_FLAGS "${FLEXFLAGS}")
	BISON_TARGET(ZqeditorParser ${CMAKE_SOURCE_DIR}/src/parser/ffscript.ypp ${ZQEDITOR_PARSER_DIR}/y.tab.cpp)
	add_flex_bison_dependency(ZqeditorLexer ZqeditorParser)
	list(APPEND ZQEDITOR_SOURCES ${BISON_ZqeditorParser_OUTPUTS} ${FLEX_ZqeditorLexer_OUTPUTS})
endif()

add_executable(zqeditor WIN32 EXCLUDE_FROM_ALL ${ZQEDITOR_SOURCES})
target_include_directories(zqeditor PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/parser
	${ZCPORT_DIR}/game
	${ZQEDITOR_PARSER_DIR})
# The game's main() becomes zcport_game_main(); see src/port/entry/zplayer_main.cpp.
# (On the target: a source file's properties would reach zplayer's copy too.)
target_compile_definitions(zqeditor PRIVATE ZCPORT_MAGIC_MAIN ZC_ZQEDITOR
	"ZPLAYER_WINDOW_TITLE=\"ZQuest Vulkan 2.53.444.7\"")
target_link_libraries(zqeditor PRIVATE zcport_sdl zcport zc_stb_vorbis zc_minimp3 zc_gme zc_dumb)
# the build catalog's other generations: zq_builds.cpp's stand-ins give way once their files are there
if(EXISTS ${ZCPORT_DIR}/zqeditor/zq_builds_old.cpp)
	target_compile_definitions(zqeditor PRIVATE ZQ_HAVE_BUILDS_OLD)
endif()
if(EXISTS ${ZCPORT_DIR}/zqeditor/zq_builds_192.cpp)
	target_compile_definitions(zqeditor PRIVATE ZQ_HAVE_BUILDS_192)
endif()
if(EXISTS ${ZCPORT_DIR}/zqeditor/zq_builds_30.cpp)
	target_compile_definitions(zqeditor PRIVATE ZQ_HAVE_BUILDS_30)
endif()
if(WIN32)
	target_link_libraries(zqeditor PRIVATE windowscodecs ole32)
endif()
set_target_properties(zqeditor PROPERTIES
	OUTPUT_NAME zqeditor
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
	VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
# As the player: signed char as on x86, no fused multiply-add (ZC_SAME_RESULTS_OPTIONS in
# CMakeLists.txt), so ZQuest computes on ARM what it computes on x64.
target_compile_options(zqeditor PRIVATE ${ZC_SAME_RESULTS_OPTIONS})
if(MSVC)
	target_compile_options(zqeditor PRIVATE /MP /wd4996 /wd4244 /wd4267 /wd4305 /wd4018)
else()
	# C++14, the standard MSVC compiles ZQuest with. The old code's narrowing initialisers
	# stay warnings.
	set_target_properties(zqeditor PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
	target_compile_options(zqeditor PRIVATE -Wno-narrowing)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(zqeditor PRIVATE -Wno-c++11-narrowing)
	endif()
	if(UNIX AND NOT APPLE)
		target_link_options(zqeditor PRIVATE -static-libstdc++ -static-libgcc)
	endif()
endif()
# 64-bit Linux and macOS (LP64): the quest code keeps 32-bit 'long' through the same
# forced include as the player (zcport/lp64.h), so files are read and written as on
# Windows. Not for the music decoders (third-party code).
if(NOT WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_zq_lp64_sources ${ZQEDITOR_SOURCES})
	# (nor the entry point, test driver and image decoder, which use no quest data: as in
	# zplayer, the entry must not see <allegro.h>, whose ZCPORT_MAGIC_MAIN renames main)
	# zq_compile_proc.cpp only starts the compiler and reads its pipes (system headers such as
	# poll.h break under the 32-bit long of lp64.h) and touches no quest data.
	list(FILTER _zq_lp64_sources EXCLUDE REGEX "zcmusic_decoders[.]cpp$|[.]c$|/entry/|autotest[.]cpp$|image_sdl[.]cpp$|zq_compile_proc[.]cpp$")
	set_property(SOURCE ${_zq_lp64_sources} APPEND PROPERTY COMPILE_OPTIONS
		-include "${ZCPORT_DIR}/include/zcport/lp64.h")
endif()

endif()
