# zscript255: upstream ZQuest Classic 2.55.17's ZScript compiler as a command-line
# program (Windows, Linux, macOS), so 2.55 quests can be recompiled as 2.55.17 would.
#
# Included from src/port/CMakeLists.txt. Its own source folder (this one) and its own
# include root: upstream's "parser/...", "base/..." headers resolve here, never to the
# 2.53 compiler in src/parser or to another compiler's folder, and no other target
# links these files. It needs only the C++ standard library (no Allegro, no SDL).
#
#   zscript255  EXE  <build>/<config>/zscript255(.exe), with the 2.55.17 ZScript headers
#                    and the compiler's base config copied to <build>/<config>/zscript255_files/
#                    (run it with -chdir there, see zscript255_main.cpp; ship that folder
#                    beside the program)
#
# Built with zqeditor (a dependency of it), or on its own with --target zscript255.
# -DZC_ZSCRIPT255=OFF leaves it out, so zqeditor builds without it; it is also left out
# (with a message) where its needs aren't met: bison 3 and flex, and a C++20 compiler
# (it is upstream's C++20 code: MSVC 2022, GCC 10 or newer, Clang 10 / Apple clang 12
# or newer).

option(ZC_ZSCRIPT255 "Build zscript255 (2.55.17's ZScript compiler) with zqeditor" ON)

set(_zs255_build OFF)
if(ZC_ZSCRIPT255 AND NOT ANDROID AND NOT NINTENDO_SWITCH)
	if(NOT BISON_FOUND)
		find_package(BISON QUIET)
	endif()
	if(NOT FLEX_FOUND)
		find_package(FLEX QUIET)
	endif()
	set(_zs255_why "")
	if(NOT BISON_FOUND OR NOT FLEX_FOUND)
		set(_zs255_why "flex and bison not found")
	elseif(BISON_VERSION VERSION_LESS 3.0)
		set(_zs255_why "bison ${BISON_VERSION} is too old (needs 3.0 or newer)")
	elseif(NOT "cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
		set(_zs255_why "the C++ compiler has no C++20")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
		set(_zs255_why "GCC ${CMAKE_CXX_COMPILER_VERSION} is too old (needs 10 or newer)")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
		set(_zs255_why "Clang ${CMAKE_CXX_COMPILER_VERSION} is too old (needs 10 or newer)")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
		set(_zs255_why "Apple clang ${CMAKE_CXX_COMPILER_VERSION} is too old (needs 12 or newer)")
	elseif(MSVC AND MSVC_VERSION LESS 1930)
		set(_zs255_why "MSVC ${MSVC_VERSION} is too old (needs Visual Studio 2022)")
	endif()
	if(_zs255_why STREQUAL "")
		set(_zs255_build ON)
	else()
		message(STATUS "zscript255: ${_zs255_why}, the 2.55.17 ZScript compiler is not built")
	endif()
endif()

if(_zs255_build)

set(ZS255_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ZS255_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/zscript255_parser)
file(MAKE_DIRECTORY ${ZS255_GEN_DIR})

if(MSVC)
	set(ZS255_FLEXFLAGS "--wincompat")
else()
	set(ZS255_FLEXFLAGS "")
endif()
# Upstream generates its scanner and parser the same way (bison 3, flex 2.6).
FLEX_TARGET(Zscript255Lexer ${ZS255_DIR}/parser/ffscript.lpp ${ZS255_GEN_DIR}/lex.yy.cpp COMPILE_FLAGS "${ZS255_FLEXFLAGS}")
BISON_TARGET(Zscript255Parser ${ZS255_DIR}/parser/ffscript.ypp ${ZS255_GEN_DIR}/y.tab.cpp)
add_flex_bison_dependency(Zscript255Lexer Zscript255Parser)

set(ZS255_SOURCES
	${ZS255_DIR}/zscript255_main.cpp
	${ZS255_DIR}/zs255_scrdata.cpp
	${ZS255_DIR}/zs255_assemble.cpp
	${ZS255_DIR}/zs255_asm_tables.cpp
	${ZS255_DIR}/zs255_files.cpp
	${ZS255_DIR}/base/general.cpp
	${ZS255_DIR}/base/qrs.cpp
	${ZS255_DIR}/base/util.cpp
	${ZS255_DIR}/parser/config.cpp
	${ZS255_DIR}/parser/AST.cpp
	${ZS255_DIR}/parser/ASTVisitors.cpp
	${ZS255_DIR}/parser/BuildVisitors.cpp
	${ZS255_DIR}/parser/ByteCode.cpp
	${ZS255_DIR}/parser/CompilerUtils.cpp
	${ZS255_DIR}/parser/CompileError.cpp
	${ZS255_DIR}/parser/CompileOption.cpp
	${ZS255_DIR}/parser/DataStructs.cpp
	${ZS255_DIR}/parser/LibrarySymbols.cpp
	${ZS255_DIR}/parser/Scope.cpp
	${ZS255_DIR}/parser/ScriptParser.cpp
	${ZS255_DIR}/parser/SemanticAnalyzer.cpp
	${ZS255_DIR}/parser/RegistrationVisitor.cpp
	${ZS255_DIR}/parser/Types.cpp
	${ZS255_DIR}/parser/ZScript.cpp
	${BISON_Zscript255Parser_OUTPUTS}
	${FLEX_Zscript255Lexer_OUTPUTS}
	# fmt 10.0.1, as upstream 2.55.17 ships it (third_party/fmt)
	${ZS255_DIR}/third_party/fmt/src/format.cc
)
file(GLOB ZS255_SYMBOL_SOURCES ${ZS255_DIR}/parser/symbols/*.cpp)
list(APPEND ZS255_SOURCES ${ZS255_SYMBOL_SOURCES})

add_executable(zscript255 EXCLUDE_FROM_ALL ${ZS255_SOURCES})
# The generated scanner and parser include the parser's headers by bare name, as
# upstream generates them inside src/parser.
target_include_directories(zscript255 PRIVATE
	${ZS255_DIR}
	${ZS255_DIR}/parser
	${ZS255_GEN_DIR}
	${ZS255_DIR}/third_party/fmt/include)
target_compile_definitions(zscript255 PRIVATE IS_PARSER)
set_target_properties(zscript255 PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	OUTPUT_NAME zscript255
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# Upstream's floating point settings, so constants fold the same way.
if(MSVC)
	target_compile_definitions(zscript255 PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
	target_compile_options(zscript255 PRIVATE /W0 /MP /fp:fast /bigobj /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(zscript255 PRIVATE -w -funsafe-math-optimizations -ffinite-math-only
		-fno-math-errno -fno-signaling-nans -fno-rounding-math -fcx-limited-range)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(zscript255 PRIVATE -w -ffp-model=fast)
endif()

# The 2.55.17 headers (std.zh and the rest) and the compiler's base config, beside
# the program, as upstream ships them beside its zscript. A folder of its own name, as a
# folder called "zscript255" would clash with the program itself where it has no ".exe".
add_custom_command(TARGET zscript255 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${ZS255_DIR}/resources $<TARGET_FILE_DIR:zscript255>/zscript255_files
	VERBATIM)

# ZQuest runs it to compile 2.55 quests' scripts.
if(TARGET zqeditor)
	add_dependencies(zqeditor zscript255)
endif()

endif()
