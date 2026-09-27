# Third-party dependencies, all fetched through CPM.cmake with pinned versions (REBUILD_PLAN §2.2).
# Set CPM_SOURCE_CACHE (e.g. ~/.cache/CPM) to share downloads between build trees.

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# Third-party code is compiled without our warning policy.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

# ---------------------------------------------------------------------------------------------
# libgit2 v1.9.6: reads, graph, diff, blame, in-memory rewrites. Transport goes through git.
# ---------------------------------------------------------------------------------------------
if(WIN32)
    set(GGUI_LIBGIT2_HTTPS Schannel)
else()
    set(GGUI_LIBGIT2_HTTPS OpenSSL-Dynamic)
endif()
set(GGUI_LIBGIT2_PATCH_COMMAND)
if(WIN32)
    # Hide console windows of processes libgit2 spawns (ssh exec).
    set(GGUI_LIBGIT2_PATCH_COMMAND PATCHES ${CMAKE_CURRENT_LIST_DIR}/libgit2-win32-no-console.patch)
endif()
CPMAddPackage(
    NAME libgit2
    GITHUB_REPOSITORY libgit2/libgit2
    GIT_TAG v1.9.6
    ${GGUI_LIBGIT2_PATCH_COMMAND}
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "BUILD_TESTS OFF"
        "BUILD_CLI OFF"
        "BUILD_EXAMPLES OFF"
        "BUILD_FUZZERS OFF"
        "EXPERIMENTAL_SHA256 ON"
        "USE_HTTPS ${GGUI_LIBGIT2_HTTPS}"
        "USE_SSH exec"
        "USE_BUNDLED_ZLIB ON"
        "REGEX_BACKEND builtin"
        "USE_HTTP_PARSER builtin"
)
add_library(ggui_libgit2 INTERFACE)
target_link_libraries(ggui_libgit2 INTERFACE libgit2package)
target_include_directories(ggui_libgit2 SYSTEM INTERFACE ${libgit2_SOURCE_DIR}/include)
target_compile_definitions(ggui_libgit2 INTERFACE GIT_EXPERIMENTAL_SHA256=1)

# ---------------------------------------------------------------------------------------------
# CLI11 (git-gg), spdlog, nlohmann/json
# ---------------------------------------------------------------------------------------------
CPMAddPackage(NAME CLI11 GITHUB_REPOSITORY CLIUtils/CLI11 GIT_TAG v2.6.2
    OPTIONS "CLI11_BUILD_TESTS OFF" "CLI11_BUILD_EXAMPLES OFF" "CLI11_BUILD_DOCS OFF")
CPMAddPackage(NAME spdlog GITHUB_REPOSITORY gabime/spdlog GIT_TAG v1.17.0
    OPTIONS "SPDLOG_BUILD_EXAMPLE OFF" "SPDLOG_BUILD_TESTS OFF")
CPMAddPackage(NAME nlohmann_json GITHUB_REPOSITORY nlohmann/json GIT_TAG v3.12.0
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")

if(NOT GGUI_BUILD_APP)
    return()
endif()

# ---------------------------------------------------------------------------------------------
# SDL3 (static), efsw, nativefiledialog-extended
# ---------------------------------------------------------------------------------------------
CPMAddPackage(NAME SDL3 GITHUB_REPOSITORY libsdl-org/SDL GIT_TAG release-3.4.4
    OPTIONS "SDL_SHARED OFF" "SDL_STATIC ON" "SDL_TEST_LIBRARY OFF" "SDL_TESTS OFF"
            "SDL_EXAMPLES OFF" "SDL_INSTALL OFF")
CPMAddPackage(NAME efsw GITHUB_REPOSITORY SpartanJ/efsw GIT_TAG 1.6.3
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_TEST_APP OFF" "EFSW_INSTALL OFF")
CPMAddPackage(NAME nfd GITHUB_REPOSITORY btzy/nativefiledialog-extended GIT_TAG v1.3.0
    OPTIONS "NFD_PORTAL ON" "NFD_BUILD_TESTS OFF" "NFD_INSTALL OFF")

# ---------------------------------------------------------------------------------------------
# Dear ImGui (docking) as a static library with our imconfig.h
# ---------------------------------------------------------------------------------------------
CPMAddPackage(NAME imgui GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG 84a9d532b6f635a6017b90e65b627c36fd1afd20 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME imgui_test_engine GITHUB_REPOSITORY ocornut/imgui_test_engine
    GIT_TAG 3fff43588c40ad225bcfff0219c62dae13ca9c47 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME stb GITHUB_REPOSITORY nothings/stb
    GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME IconFontCppHeaders GITHUB_REPOSITORY juliettef/IconFontCppHeaders
    GIT_TAG 4577f2f72c36856ae3f2808fe013f669b7cb64e9 DOWNLOAD_ONLY YES)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp
    ${PROJECT_SOURCE_DIR}/Source)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imconfig.h")
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

if(GGUI_ENABLE_IMGUI_TEST_ENGINE)
    file(GLOB GGUI_TE_SOURCES ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/*.cpp)
    target_sources(imgui PRIVATE ${GGUI_TE_SOURCES})
    target_include_directories(imgui SYSTEM PUBLIC
        ${imgui_test_engine_SOURCE_DIR} ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine
        ${stb_SOURCE_DIR})
    target_compile_definitions(imgui PUBLIC
        IMGUI_ENABLE_TEST_ENGINE
        IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
        IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1
        IMGUI_TEST_ENGINE_ENABLE_STD_FUNCTION=1
        IMGUI_TEST_ENGINE_ENABLE_IMPLOT=0
        GGUI_ENABLE_IMGUI_TEST_ENGINE=1)
    find_package(Threads REQUIRED)
    target_link_libraries(imgui PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------------------------
# ImGuiColorTextEdit (goossens): TextEditor + TextDiff, patched with a hash-stamped re-apply
# ---------------------------------------------------------------------------------------------
CPMAddPackage(NAME ImGuiColorTextEdit GITHUB_REPOSITORY goossens/ImGuiColorTextEdit
    GIT_TAG fa6fd434d3db973e604653d2147316f41d49ed56 DOWNLOAD_ONLY YES)
set(GGUI_TE_PATCH ${CMAKE_CURRENT_LIST_DIR}/ImGuiColorTextEdit.patch)
if(EXISTS ${GGUI_TE_PATCH})
    file(SHA256 ${GGUI_TE_PATCH} GGUI_TE_PATCH_HASH)
    set(GGUI_TE_STAMP ${ImGuiColorTextEdit_SOURCE_DIR}/.ggui-patch-stamp)
    set(GGUI_TE_OLD_HASH "")
    if(EXISTS ${GGUI_TE_STAMP})
        file(READ ${GGUI_TE_STAMP} GGUI_TE_OLD_HASH)
    endif()
    if(NOT GGUI_TE_OLD_HASH STREQUAL GGUI_TE_PATCH_HASH)
        find_package(Git REQUIRED)
        # Undo a previously applied (older) patch, then apply the current one.
        execute_process(COMMAND ${GIT_EXECUTABLE} -c safe.directory=* checkout -- .
            WORKING_DIRECTORY ${ImGuiColorTextEdit_SOURCE_DIR} RESULT_VARIABLE _r)
        execute_process(COMMAND ${GIT_EXECUTABLE} -c safe.directory=* apply --whitespace=nowarn ${GGUI_TE_PATCH}
            WORKING_DIRECTORY ${ImGuiColorTextEdit_SOURCE_DIR} RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${GGUI_TE_PATCH}")
        endif()
        file(WRITE ${GGUI_TE_STAMP} ${GGUI_TE_PATCH_HASH})
    endif()
endif()
file(GLOB GGUI_CTE_SOURCES ${ImGuiColorTextEdit_SOURCE_DIR}/*.cpp)
add_library(imgui_color_text_edit STATIC ${GGUI_CTE_SOURCES})
target_include_directories(imgui_color_text_edit SYSTEM PUBLIC ${ImGuiColorTextEdit_SOURCE_DIR})
target_link_libraries(imgui_color_text_edit PUBLIC imgui)

add_library(icon_font_headers INTERFACE)
target_include_directories(icon_font_headers SYSTEM INTERFACE ${IconFontCppHeaders_SOURCE_DIR})

# ---------------------------------------------------------------------------------------------
# Fonts (downloaded once, embedded at build time by EmbedFont.cmake)
# ---------------------------------------------------------------------------------------------
set(GGUI_FONT_DIR ${CMAKE_BINARY_DIR}/fonts)
function(ggui_fetch_font out url sha256)
    set(dest ${GGUI_FONT_DIR}/${out})
    if(NOT EXISTS ${dest})
        file(DOWNLOAD ${url} ${dest} EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON)
    endif()
endfunction()
ggui_fetch_font(MaterialSymbolsOutlined.ttf
    "https://raw.githubusercontent.com/google/material-design-icons/bd8cb85bd4bad964fe6918f79665bb40c3a8efef/variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf"
    0128da5981791d2fe09918c337ea6657b0ef2e4e5f8f4af3e1dc5f31889b2311)
ggui_fetch_font(NotoSansMono.ttf
    "https://raw.githubusercontent.com/google/fonts/23e54b51ddffbc7713c583748e3bd86f62b1fa4a/ofl/notosansmono/NotoSansMono%5Bwdth%2Cwght%5D.ttf"
    2cb2adb378a8f574213e23df697050b83c54c27df465a2015552740b2769a081)
if(NOT EXISTS ${GGUI_FONT_DIR}/JetBrainsMono.ttf)
    file(DOWNLOAD https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
        ${GGUI_FONT_DIR}/jb.zip EXPECTED_HASH SHA256=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf)
    file(ARCHIVE_EXTRACT INPUT ${GGUI_FONT_DIR}/jb.zip DESTINATION ${GGUI_FONT_DIR}/jb
        PATTERNS fonts/ttf/JetBrainsMono-Regular.ttf)
    file(COPY_FILE ${GGUI_FONT_DIR}/jb/fonts/ttf/JetBrainsMono-Regular.ttf ${GGUI_FONT_DIR}/JetBrainsMono.ttf)
    file(REMOVE_RECURSE ${GGUI_FONT_DIR}/jb ${GGUI_FONT_DIR}/jb.zip)
endif()
