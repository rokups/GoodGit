cmake_minimum_required(VERSION 3.25)
project(ggui VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GGUI_BUILD_APP "Build the ggui desktop app (off builds git-gg only)" ON)
option(GGUI_ENABLE_IMGUI_TEST_ENGINE "Compile the integration tests into ggui (--test)" ON)
option(GGUI_WERROR "Treat warnings in first-party code as errors" ON)
option(GGUI_STATIC_CXX_RUNTIME
    "Link libstdc++/libgcc statically into ggui and git-gg on Linux (portable packages)" OFF)
set(GGUI_COVERAGE "OFF" CACHE STRING "Coverage instrumentation: OFF, CLANG (source-based) or GCOV")
set_property(CACHE GGUI_COVERAGE PROPERTY STRINGS OFF CLANG GCOV)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(UNIX AND NOT APPLE)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# Install rules of the fetched dependencies (headers, static libraries) land in a component that
# is never packaged; ggui's own files are the "ggui" component (cmake/Packaging.cmake).
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME thirdparty)
include(cmake/Dependencies.cmake)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME ggui)

# ggui and git-gg live side by side (as when installed), so tests can put git-gg on PATH.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
include(cmake/EmbedFont.cmake)

# Warning, sanitizer-free coverage and UI-thread-check settings shared by first-party targets.
function(ggui_first_party target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(GGUI_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow
            -Wno-missing-field-initializers)
        if(GGUI_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(GGUI_COVERAGE STREQUAL "CLANG")
        target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(${target} PRIVATE -fprofile-instr-generate)
    elseif(GGUI_COVERAGE STREQUAL "GCOV")
        target_compile_options(${target} PRIVATE --coverage -fprofile-update=atomic)
        target_link_options(${target} PRIVATE --coverage)
    endif()
    # The UI-thread assertion (§3.1) is compiled into debug and test builds.
    if(GGUI_ENABLE_IMGUI_TEST_ENGINE)
        target_compile_definitions(${target} PRIVATE GGUI_THREAD_CHECKS=1)
    else()
        target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:GGUI_THREAD_CHECKS=1>)
    endif()
    if(WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
            _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

# Settings for the two shipped executables: static runtime and the Windows version resource.
#   ggui_executable(<target> <description>)
function(ggui_executable target description)
    if(MINGW)
        # Fully static: no libstdc++-6.dll, libgcc_s_seh-1.dll or libwinpthread-1.dll to ship.
        target_link_options(${target} PRIVATE -static)
    elseif(UNIX AND NOT APPLE AND GGUI_STATIC_CXX_RUNTIME)
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    if(WIN32)
        set(GGUI_RC_NAME ${target})
        set(GGUI_RC_DESCRIPTION "${description}")
        set(GGUI_RC_ICON ${PROJECT_SOURCE_DIR}/res/ggui.ico)
        set(rc ${CMAKE_BINARY_DIR}/generated/${target}.rc)
        configure_file(${PROJECT_SOURCE_DIR}/res/version.rc.in ${rc} @ONLY)
        target_sources(${target} PRIVATE ${rc})
    endif()
endfunction()

add_subdirectory(Source/libgg)
add_subdirectory(Source/gitgg)
if(GGUI_BUILD_APP)
    add_subdirectory(Source/core)
    add_subdirectory(Source/app)
endif()

include(cmake/Packaging.cmake)
