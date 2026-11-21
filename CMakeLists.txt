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
option(GGUI_SANITIZE "Build everything with AddressSanitizer and UBSan (gcc/clang on Linux)" OFF)
set(GGUI_COVERAGE "OFF" CACHE STRING "Coverage instrumentation: OFF, CLANG (source-based) or GCOV")
set_property(CACHE GGUI_COVERAGE PROPERTY STRINGS OFF CLANG GCOV)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# All targets, third party too: a mix of instrumented and plain code gives false reports in ASan.
if(GGUI_SANITIZE)
    if(NOT (UNIX AND NOT APPLE))
        message(FATAL_ERROR "GGUI_SANITIZE needs gcc or clang on Linux")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
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

# Release hardening. An unsupported flag is skipped, not an error. Compile flags go to the first-party
# targets (ggui_first_party), link flags to the two executables (ggui_executable). PIE comes from
# CMAKE_POSITION_INDEPENDENT_CODE with policy CMP0083.
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
set(GGUI_HARDEN_COMPILE "")
set(GGUI_HARDEN_LINK "")
function(ggui_try_flag kind flag)
    string(MAKE_C_IDENTIFIER "GGUI_HAVE_${kind}_${flag}" var)
    if(kind STREQUAL "COMPILE")
        check_cxx_compiler_flag("${flag}" ${var})
    else()
        check_linker_flag(CXX "${flag}" ${var})
    endif()
    if(${var})
        list(APPEND GGUI_HARDEN_${kind} "${flag}")
        set(GGUI_HARDEN_${kind} "${GGUI_HARDEN_${kind}}" PARENT_SCOPE)
    endif()
endfunction()
if(MSVC)
    ggui_try_flag(COMPILE /guard:cf)
    ggui_try_flag(LINK /GUARD:CF)
    ggui_try_flag(LINK /DYNAMICBASE)
    ggui_try_flag(LINK /NXCOMPAT)
else()
    if(UNIX AND NOT APPLE)
        ggui_try_flag(COMPILE -fstack-protector-strong)
        ggui_try_flag(COMPILE -fstack-clash-protection)
        ggui_try_flag(LINK -Wl,-z,relro,-z,now)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
            ggui_try_flag(COMPILE -fcf-protection)
        endif()
    elseif(MINGW)
        ggui_try_flag(LINK -Wl,--dynamicbase,--nxcompat)
    endif()
endif()
# Paths of this machine stay out of the binary (also for the fetched sources).
set(GGUI_PATH_MAP "")
if(MSVC)
    check_cxx_compiler_flag("/pathmap:${CMAKE_SOURCE_DIR}=." GGUI_HAVE_PATHMAP)
    if(GGUI_HAVE_PATHMAP)
        set(GGUI_PATH_MAP "/pathmap:${CMAKE_SOURCE_DIR}=.")
    endif()
else()
    check_cxx_compiler_flag("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." GGUI_HAVE_PREFIX_MAP)
    if(GGUI_HAVE_PREFIX_MAP)
        set(GGUI_PATH_MAP "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
    endif()
endif()

# Warning, sanitizer-free coverage and UI-thread-check settings shared by first-party targets.
function(ggui_first_party target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus
            /wd4127)  # conditional expression is constant: do/while(0) in IM_CHECK and friends
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
    # Release only. _FORTIFY_SOURCE needs optimization, so it is not set for the other configs.
    target_compile_options(${target} PRIVATE
        "$<$<CONFIG:Release>:${GGUI_HARDEN_COMPILE};${GGUI_PATH_MAP}>")
    if(UNIX AND NOT APPLE)
        # One -Wp option keeps the order: CMake puts definitions before options, so a separate -U
        # would undo the -D. The -U stops a "redefined" warning where gcc sets a default level.
        target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:-Wp,-U_FORTIFY_SOURCE,-D_FORTIFY_SOURCE=3>")
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
    target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:${GGUI_HARDEN_LINK}>")
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
        # MSVC merges the .manifest source into the linker manifest; windres needs an RT_MANIFEST line.
        set(GGUI_RC_MANIFEST_LINE "")
        if(MINGW)
            set(GGUI_RC_MANIFEST_LINE "1 24 \"${PROJECT_SOURCE_DIR}/res/ggui.manifest\"")
        endif()
        set(rc ${CMAKE_BINARY_DIR}/generated/${target}.rc)
        configure_file(${PROJECT_SOURCE_DIR}/res/version.rc.in ${rc} @ONLY)
        target_sources(${target} PRIVATE ${rc})
        if(MSVC)
            target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/res/ggui.manifest)
        endif()
    endif()
endfunction()

# Build identity (Source/app/util/BuildInfo.cpp): the package flavour names the release asset that
# matches this build; the commit is set by CI only.
if(WIN32)
    if(MSVC)
        set(GGUI_PACKAGE_SYSTEM windows-x64-msvc)
    else()
        set(GGUI_PACKAGE_SYSTEM windows-x64-mingw)
    endif()
else()
    set(GGUI_PACKAGE_SYSTEM linux-${CMAKE_SYSTEM_PROCESSOR})
endif()
set(GGUI_BUILD_COMMIT "" CACHE STRING "Commit of the build; empty: GITHUB_SHA at build time, else none")
set(GGUI_BUILD_IDENTITY_CPP ${CMAKE_BINARY_DIR}/generated/BuildIdentity.cpp)
# Each build runs the script: the commit and its time (GGUI_BUILD_TIME) match the sources.
add_custom_target(ggui_build_identity BYPRODUCTS ${GGUI_BUILD_IDENTITY_CPP}
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${GGUI_BUILD_IDENTITY_CPP}
        -DCOMMIT=${GGUI_BUILD_COMMIT} -P ${PROJECT_SOURCE_DIR}/cmake/BuildIdentity.cmake
    COMMENT "Build identity")

add_subdirectory(Source/libgg)
add_subdirectory(Source/gitgg)
if(GGUI_BUILD_APP)
    add_subdirectory(Source/core)
    add_subdirectory(Source/app)
endif()

include(cmake/Packaging.cmake)
