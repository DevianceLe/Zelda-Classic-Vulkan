# zscript30: ZQuest Classic 3.0's ZScript compiler ("zscript" upstream) as a command-line
# program without Allegro, for ZQuest to compile the scripts of 3.0 quests the way
# upstream's editor does (src/zq/dialog/compilezscript.cpp runs "zscript -linked ...").
#
# Sources (ZQuest Classic 3.0 at ef321f8, GPL), copied unchanged except where marked
# "zscript30:":
#   parser/            src/parser (the compiler, its flex and bison grammars, its tests)
#   base/              src/base (no http or emscripten parts)
#   components/zasm/   src/components/zasm (the ZASM command and register tables)
#   core/qrs.*         src/core/qrs.* (quest rules given with -qr)
#   core/zdefs.h       the part of src/core/zdefs.h the compiler uses
#   zscrdata.h         src/zscrdata.h (the "tmp2" results file, ZASM metadata text)
#   zalleg/zsyssimple.h, test_runner/   as upstream
#   zconsole/ConsoleLogger.h            stands in for upstream's (pipe to the editor only)
#   third_party/       fmt, nlohmann json, nonstd expected (their licences inside), and
#                      pstream.h (upstream's include/pstream, Boost licence, in the file)
#   resources/         resources/{include,headers,base_config}: the 3.0 ZScript headers
#                      and the compiler's base config
#   windows.manifest   src/windows.manifest (UTF-8 as the process code page on Windows)
#
# The program, as upstream's, reads base_config/zscript.cfg, the default include paths
# (include/;headers/;scripts/) and imports from its current directory. On Windows and
# macOS it changes the current directory to its own folder on start (ZC_DISABLE_CHDIR=1
# turns that off); on Linux it doesn't (upstream only moves from a bin/ folder to
# ../share/zquestclassic), so whoever starts it starts it in its folder. So it is built
# into its own folder, zscript30/, with the resources copied beside it: the folder above
# holds 2.53's std.zh and friends, which an `import "std.zh"` (tried in the current
# directory first) would otherwise find.
#
# The compiler needs C++20 (upstream's standard); the rest of the project stays as it is.
#
# Never give these sources the port's forced include zcport/lp64.h (the player and editor
# get it per source on 64-bit Unix): it redefines 'long', and upstream's code here uses
# 'unsigned long long' where the C++ standard requires it (base/zfix.h's literal operators).

cmake_minimum_required(VERSION 3.16)

set(ZS30_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Nothing from the directories above: the top level adds 2.53's Allegro and library include
# folders and ALLEGRO_* definitions to every directory. The compiler takes its headers
# only from here (compile options, such as the C runtime and /fp:fast, are kept).
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")
set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS "")

if(NOT BISON_FOUND)
	find_package(BISON 3.0 QUIET)
endif()
if(NOT FLEX_FOUND)
	find_package(FLEX QUIET)
endif()
if(NOT (BISON_FOUND AND FLEX_FOUND))
	message(WARNING "zscript30: flex and bison (>= 3.0) not found; the 3.0 ZScript compiler is not built.")
	return()
endif()
# The grammar uses bison 3 syntax (%define api.token.prefix {TOK_}); macOS's own bison is 2.3,
# so a newer one (Homebrew's) has to be given with -DBISON_EXECUTABLE there.
if(BISON_VERSION VERSION_LESS 3.0)
	message(WARNING "zscript30: bison ${BISON_VERSION} is too old (3.0 or newer needed); the 3.0 ZScript compiler is not built.")
	return()
endif()

# The lexer and parser are generated into the build folder (upstream: into src/parser).
set(ZS30_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${ZS30_GEN_DIR})
set(ZS30_FLEXFLAGS "")
if(MSVC)
	set(ZS30_FLEXFLAGS "--wincompat")
endif()
FLEX_TARGET(ZScript30Lexer ${ZS30_DIR}/parser/ffscript.lpp ${ZS30_GEN_DIR}/lex.yy.cpp COMPILE_FLAGS "${ZS30_FLEXFLAGS}")
BISON_TARGET(ZScript30Parser ${ZS30_DIR}/parser/ffscript.ypp ${ZS30_GEN_DIR}/y.tab.cpp COMPILE_FLAGS -v)
add_flex_bison_dependency(ZScript30Lexer ZScript30Parser)

set(ZS30_SOURCES
	# upstream modules/zscript/ParserCore.txt
	${ZS30_DIR}/parser/parser.cpp
	${ZS30_DIR}/parser/config.cpp
	${ZS30_DIR}/core/qrs.cpp
	# upstream modules/zscript/ParserSrc.txt
	${ZS30_DIR}/parser/AST.cpp
	${ZS30_DIR}/parser/ASTVisitors.cpp
	${ZS30_DIR}/parser/BuildVisitors.cpp
	${ZS30_DIR}/parser/CommentUtils.cpp
	${ZS30_DIR}/parser/CompilerUtils.cpp
	${ZS30_DIR}/parser/CompileError.cpp
	${ZS30_DIR}/parser/CompileOption.cpp
	${ZS30_DIR}/parser/DataStructs.cpp
	${ZS30_DIR}/parser/LibrarySymbols.cpp
	${ZS30_DIR}/parser/Opcode.cpp
	${ZS30_DIR}/parser/Scope.cpp
	${ZS30_DIR}/parser/ParserHelper.cpp
	${ZS30_DIR}/parser/ScriptParser.cpp
	${ZS30_DIR}/parser/RegBaseVisitor.cpp
	${ZS30_DIR}/parser/SemanticAnalyzer.cpp
	${ZS30_DIR}/parser/RegistrationVisitor.cpp
	${ZS30_DIR}/parser/ReturnVisitor.cpp
	${ZS30_DIR}/parser/MetadataVisitor.cpp
	${ZS30_DIR}/parser/DocVisitor.cpp
	${ZS30_DIR}/parser/Types.cpp
	${ZS30_DIR}/parser/ZScript.cpp
	${ZS30_DIR}/test_runner/test_runner.cpp
	${ZS30_DIR}/parser/parser_test.cpp
	# zscript30: -assembled, upstream ZQuest's ZASM text step (src/zq/ffasm.cpp)
	${ZS30_DIR}/zq/zq_assemble.cpp
	# upstream's base library
	${ZS30_DIR}/base/about.cpp
	${ZS30_DIR}/base/check.cpp
	${ZS30_DIR}/base/containers.cpp
	${ZS30_DIR}/base/crash_backtrace.cpp
	${ZS30_DIR}/base/general.cpp
	${ZS30_DIR}/base/md5.cpp
	${ZS30_DIR}/base/process_management.cpp
	${ZS30_DIR}/base/random.cpp
	${ZS30_DIR}/base/sin1.cpp
	${ZS30_DIR}/base/util.cpp
	${ZS30_DIR}/base/version.cpp
	${ZS30_DIR}/base/win_encoding.cpp
	${ZS30_DIR}/base/zapp.cpp
	${ZS30_DIR}/base/zc_math.cpp
	${ZS30_DIR}/base/zfix.cpp
	# upstream's zasm component
	${ZS30_DIR}/components/zasm/debug_data.cpp
	${ZS30_DIR}/components/zasm/eval.cpp
	${ZS30_DIR}/components/zasm/inst_var_remap.cpp
	${ZS30_DIR}/components/zasm/serialize.cpp
	${ZS30_DIR}/components/zasm/table.cpp
	# fmt (upstream builds it as a library)
	${ZS30_DIR}/third_party/fmt/src/format.cc
)

# Built on request, as zqeditor (--target zscript30, or as a dependency of a target that
# runs it).
add_executable(zscript30 EXCLUDE_FROM_ALL ${ZS30_SOURCES} ${BISON_ZScript30Parser_OUTPUTS} ${FLEX_ZScript30Lexer_OUTPUTS})
# Its own folder, with the resources beside it (see above): "zscript30" in the folder
# zqeditor and zplayer are built into (<build>/<config>/ with Visual Studio and Xcode,
# <build>/ with Makefiles and Ninja).
get_property(ZS30_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(ZS30_MULTI_CONFIG)
	set(ZS30_OUT_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/zscript30)
else()
	set(ZS30_OUT_DIR ${CMAKE_BINARY_DIR}/zscript30)
endif()
set_target_properties(zscript30 PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	RUNTIME_OUTPUT_DIRECTORY ${ZS30_OUT_DIR})
target_include_directories(zscript30 PRIVATE
	${ZS30_DIR}
	${ZS30_GEN_DIR}
	${ZS30_DIR}/third_party/fmt/include
	${ZS30_DIR}/third_party/json/include
	${ZS30_DIR}/third_party/nonstd/include)
if(NOT WIN32)
	# Upstream's include/pstream: the pipe to the editor on Linux and macOS
	# (base/process_management.cpp).
	target_include_directories(zscript30 PRIVATE ${ZS30_DIR}/third_party/pstream)
endif()
# As upstream's zscript target (DCHECK()s are checked in Debug builds, as upstream's).
target_compile_definitions(zscript30 PRIVATE IS_PARSER NOMINMAX ZC_APP_NAME="zscript" _CRT_SECURE_NO_WARNINGS
	"$<$<CONFIG:Debug>:DCHECK_IS_ON>")
if(MSVC)
	target_compile_definitions(zscript30 PRIVATE __STDC_LIMIT_MACROS)
	# Upstream's flags: UTF-8 sources (fmt needs it), the conforming preprocessor, and
	# RelWithDebInfo's inlining (/Ob1). Fast floating point (/fp:fast, as upstream builds
	# zscript) is set here for every configuration; the top level adds it to Release only.
	# /bigobj for the large generated parser.
	target_compile_options(zscript30 PRIVATE /utf-8 /Zc:preprocessor /EHsc /bigobj /fp:fast
		"$<$<CONFIG:Release>:/Ob1>" "$<$<CONFIG:Release>:/Zi>")
	# Upstream's manifest: UTF-8 as the process code page (Windows 10 1903 and newer), so
	# argv, fopen and narrow std::filesystem paths take UTF-8 as upstream's zscript does.
	# Without it a quest folder, console file or user name with a letter outside the ANSI
	# code page can't be opened, and other non-ASCII paths reach the JSON, the console text
	# and the debug data in "tmp2" in the ANSI code page instead of UTF-8.
	target_sources(zscript30 PRIVATE ${ZS30_DIR}/windows.manifest)
	# No console window when the editor starts it (upstream: /SUBSYSTEM:WINDOWS too), with
	# a plain main() as the entry point; its output still reaches redirected handles.
	# Upstream's /LARGEADDRESSAWARE (a 32-bit build compiling a big quest's scripts) and
	# /OPT:NOICF (no merging of identical functions) too.
	target_link_options(zscript30 PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /LARGEADDRESSAWARE /OPT:NOICF
		"$<$<CONFIG:Release>:/DEBUG>")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(zscript30 PRIVATE -funsafe-math-optimizations -ffinite-math-only -fno-math-errno -fno-signaling-nans -fno-rounding-math -fcx-limited-range)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(zscript30 PRIVATE -ffp-model=fast)
endif()
if(WIN32)
	target_link_libraries(zscript30 PRIVATE shell32)
elseif(NOT APPLE)
	find_package(Threads)
	target_link_libraries(zscript30 PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# The 3.0 ZScript headers and the base config, beside the program.
add_custom_command(TARGET zscript30 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${ZS30_DIR}/resources $<TARGET_FILE_DIR:zscript30>
	COMMENT "zscript30: copying the 3.0 ZScript headers beside the program")
