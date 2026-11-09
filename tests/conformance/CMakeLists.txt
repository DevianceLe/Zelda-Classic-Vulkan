# Conformance harness: every tests/conformance/test_<name>.cpp becomes
#   conf_<name>_ref   compiled against allegro/include, linked with alleg44.lib
#   conf_<name>_port  compiled against src/port/include, linked with zcport
# (*_portonly.cpp: port program only; *_sdl*: the port program also links
# zcport_sdl, and both sides default to SYSTEM_AUTODETECT).
# All programs land in ${CMAKE_BINARY_DIR}/conformance/<config>/ next to a copy
# of alleg44.dll. Compare them with tests/conformance/run_conformance.py.
#
# Two ways to configure:
#   1. Included from the game build (src/port/CMakeLists.txt or the top level):
#        add_subdirectory(${CMAKE_SOURCE_DIR}/tests/conformance
#                         ${CMAKE_BINARY_DIR}/tests/conformance)
#      AFTER zcport (and zcport_sdl) are defined: the port programs are only
#      created when those targets exist at that point.
#   2. Stand-alone, for quick iteration without the game, SDL3 or bison/flex:
#        cmake -S tests/conformance -B <dir> -G "Visual Studio 17 2022" -A Win32
#      zcport is then built here from src/port/al/*.cpp (CONF_BUILD_ZCPORT).

cmake_minimum_required(VERSION 3.16)

get_filename_component(CONF_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)
    project(ZCConformance C CXX)
    set(CONF_STANDALONE ON)

    # Same compiler settings as the game build (top-level CMakeLists.txt).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        add_compile_definitions(ALLEGRO_MSVC ALLEGRO_HAVE_STDINT_H _CRT_SECURE_NO_WARNINGS)
        add_compile_options("$<$<CONFIG:RELEASE>:/fp:fast>")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCD /DEBUG")
    endif()

    option(CONF_BUILD_ZCPORT "Stand-alone mode: build zcport from <CONF_PORT_DIR>/al/*.cpp" ON)
    set(CONF_PORT_DIR "${CONF_ROOT}/src/port" CACHE PATH
        "Stand-alone mode: port tree with include/ and al/ (default src/port)")
    if(CONF_BUILD_ZCPORT AND NOT TARGET zcport)
        file(GLOB CONF_ZCPORT_SOURCES CONFIGURE_DEPENDS "${CONF_PORT_DIR}/al/*.cpp")
        if(CONF_ZCPORT_SOURCES AND EXISTS "${CONF_PORT_DIR}/include/allegro.h")
            # Mirrors the zcport convention: STATIC, PUBLIC include dir
            # src/port/include, PUBLIC ZC_PORT=1. No SDL (zcport_sdl is not
            # built in stand-alone mode, so *_sdl tests get no port program).
            add_library(zcport STATIC ${CONF_ZCPORT_SOURCES})
            target_include_directories(zcport PUBLIC "${CONF_PORT_DIR}/include")
            target_compile_definitions(zcport PUBLIC ZC_PORT=1)
            set_target_properties(zcport PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
            message(STATUS "conformance: stand-alone zcport from ${CONF_PORT_DIR}/al")
        else()
            message(STATUS "conformance: no ${CONF_PORT_DIR}/al/*.cpp or include/allegro.h yet; no zcport")
        endif()
    endif()
else()
    set(CONF_STANDALONE OFF)
endif()

# The game build adds allegro/include (and others) to every directory. The port
# programs must see only src/port/include, so start this directory clean and
# add include paths per target.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

# The top level forces /SUBSYSTEM:WINDOWS on executables; these are console
# programs.
if(MSVC)
    string(REPLACE "/SUBSYSTEM:WINDOWS" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
endif()

set(CONF_ALLEGRO_INCLUDE "${CONF_ROOT}/allegro/include")
set(CONF_ALLEGRO_LIB "${CONF_ROOT}/libs/win32/alleg44.lib")
set(CONF_ALLEGRO_DLL "${CONF_ROOT}/bin/win32/alleg44.dll")
set(CONF_DEFAULT_DATA_DIR "${CONF_ROOT}/build/Release" CACHE PATH
    "Default --data directory of the conformance programs")
get_filename_component(conf_quests_default "${CONF_ROOT}/../quests" ABSOLUTE)
set(CONF_DEFAULT_QUESTS_DIR "${conf_quests_default}" CACHE PATH
    "Default --quests directory (custom quests) of the conformance programs")
set(CONF_OUTPUT_DIR "${CMAKE_BINARY_DIR}/conformance")

set(CONF_FRAMEWORK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/conf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/conf_crash.cpp
)
set(CONF_FRAMEWORK_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/conf_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/conf_zc.h
)

file(GLOB CONF_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(SORT CONF_TEST_SOURCES)

set(CONF_HAVE_PORT OFF)
if(TARGET zcport)
    set(CONF_HAVE_PORT ON)
endif()
set(CONF_HAVE_PORT_SDL OFF)
if(TARGET zcport_sdl)
    set(CONF_HAVE_PORT_SDL ON)
endif()
# The reference side links the prebuilt 32-bit alleg44.lib: 32-bit builds only.
# A 64-bit build gets the port programs, whose output can be compared with the
# reference programs of a 32-bit build.
set(CONF_HAVE_REF ON)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CONF_HAVE_REF OFF)
endif()

# Settings shared by the framework objects and the test programs of one side.
function(conf_side_settings tgt side)
    target_include_directories(${tgt} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${tgt} PRIVATE
        ALLEGRO_NO_COMPATIBILITY
        "CONF_DEFAULT_DATA_DIR=\"${CONF_DEFAULT_DATA_DIR}\""
        "CONF_DEFAULT_QUESTS_DIR=\"${CONF_DEFAULT_QUESTS_DIR}\""
    )
    if(MSVC)
        target_compile_definitions(${tgt} PRIVATE ALLEGRO_MSVC ALLEGRO_HAVE_STDINT_H _CRT_SECURE_NO_WARNINGS)
    endif()
    if(side STREQUAL "ref")
        target_include_directories(${tgt} PRIVATE "${CONF_ALLEGRO_INCLUDE}")
        target_compile_definitions(${tgt} PRIVATE CONF_SIDE_REF=1)
    else()
        target_compile_definitions(${tgt} PRIVATE CONF_SIDE_PORT=1)
        target_link_libraries(${tgt} PRIVATE zcport)
    endif()
    set_target_properties(${tgt} PROPERTIES FOLDER conformance)
endfunction()

function(conf_program tgt side test_src fw_lib is_sdl)
    add_executable(${tgt} ${test_src})
    conf_side_settings(${tgt} ${side})
    target_link_libraries(${tgt} PRIVATE ${fw_lib})
    if(is_sdl)
        target_compile_definitions(${tgt} PRIVATE CONF_SDL_TEST=1)
    endif()
    if(side STREQUAL "ref")
        target_link_libraries(${tgt} PRIVATE "${CONF_ALLEGRO_LIB}")
        add_dependencies(${tgt} conf_copy_alleg_dll)
    elseif(is_sdl)
        target_link_libraries(${tgt} PRIVATE zcport_sdl)
    endif()
    set_target_properties(${tgt} PROPERTIES
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CONF_OUTPUT_DIR}/$<CONFIG>"
        PDB_OUTPUT_DIRECTORY "${CONF_OUTPUT_DIR}/$<CONFIG>"
    )
    if(MSVC)
        target_link_options(${tgt} PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
endfunction()

if(CONF_HAVE_REF)
    # alleg44.dll next to the programs (one copy step, shared by all _ref targets).
    add_custom_target(conf_copy_alleg_dll
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CONF_OUTPUT_DIR}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CONF_ALLEGRO_DLL}" "${CONF_OUTPUT_DIR}/$<CONFIG>/"
        VERBATIM
    )
    set_target_properties(conf_copy_alleg_dll PROPERTIES FOLDER conformance)
endif()

# The framework, compiled once per side.
if(CONF_HAVE_REF)
    add_library(conf_framework_ref OBJECT ${CONF_FRAMEWORK_SOURCES} ${CONF_FRAMEWORK_HEADERS})
    conf_side_settings(conf_framework_ref ref)
endif()
if(CONF_HAVE_PORT)
    add_library(conf_framework_port OBJECT ${CONF_FRAMEWORK_SOURCES} ${CONF_FRAMEWORK_HEADERS})
    conf_side_settings(conf_framework_port port)
endif()

set(CONF_REF_TARGETS "")
set(CONF_PORT_TARGETS "")
set(CONF_SKIPPED_PORT "")

foreach(src ${CONF_TEST_SOURCES})
    get_filename_component(base "${src}" NAME_WE)
    string(REGEX REPLACE "^test_" "" name "${base}")

    set(portonly OFF)
    if(name MATCHES "_portonly$")
        set(portonly ON)
    endif()
    set(is_sdl OFF)
    if(name MATCHES "_sdl")
        set(is_sdl ON)
    endif()

    if(CONF_HAVE_REF AND NOT portonly)
        conf_program(conf_${name}_ref ref "${src}" conf_framework_ref ${is_sdl})
        list(APPEND CONF_REF_TARGETS conf_${name}_ref)
    endif()

    if(CONF_HAVE_PORT AND (NOT is_sdl OR CONF_HAVE_PORT_SDL))
        conf_program(conf_${name}_port port "${src}" conf_framework_port ${is_sdl})
        list(APPEND CONF_PORT_TARGETS conf_${name}_port)
    else()
        list(APPEND CONF_SKIPPED_PORT conf_${name}_port)
    endif()
endforeach()

# Convenience targets: conf_all_ref, conf_all_port, conf_all.
add_custom_target(conf_all_ref)
set_target_properties(conf_all_ref PROPERTIES FOLDER conformance)
if(CONF_REF_TARGETS)
    add_dependencies(conf_all_ref ${CONF_REF_TARGETS})
endif()
add_custom_target(conf_all_port)
set_target_properties(conf_all_port PROPERTIES FOLDER conformance)
if(CONF_PORT_TARGETS)
    add_dependencies(conf_all_port ${CONF_PORT_TARGETS})
endif()
add_custom_target(conf_all)
add_dependencies(conf_all conf_all_ref conf_all_port)
set_target_properties(conf_all PROPERTIES FOLDER conformance)

list(LENGTH CONF_REF_TARGETS conf_n_ref)
list(LENGTH CONF_PORT_TARGETS conf_n_port)
message(STATUS "conformance: ${conf_n_ref} reference and ${conf_n_port} port programs -> ${CONF_OUTPUT_DIR}/<config>")
if(CONF_SKIPPED_PORT)
    if(NOT CONF_HAVE_PORT)
        message(STATUS "conformance: target zcport does not exist (yet); port programs skipped")
    else()
        message(STATUS "conformance: target zcport_sdl does not exist; skipped ${CONF_SKIPPED_PORT}")
    endif()
endif()
