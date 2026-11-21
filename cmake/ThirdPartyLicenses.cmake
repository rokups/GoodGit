# Writes ${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES.txt from the fetched CPM sources, so the texts
# follow the pinned versions. Components that this build does not fetch are skipped.
# Test-only packages (ImGui test engine, stb) are not listed: release builds do not ship them.
set(GGUI_TPL_FILE ${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES.txt)
set(GGUI_TPL_DIR ${CMAKE_CURRENT_LIST_DIR}/licenses)
file(WRITE ${GGUI_TPL_FILE}
    "Third-party components in ggui and git-gg. The ggui license is in LICENSE.\n")

# ggui_tpl_add(<label> <package or ""> <license id> <license file>)
# No version is listed: the pins in cmake/Dependencies.cmake are not variables, so a copy would drift.
# A package license file is relative to the CPM source; otherwise it is in cmake/licenses.
function(ggui_tpl_add label pkg spdx file)
    if(pkg)
        if(NOT DEFINED ${pkg}_SOURCE_DIR)
            return()
        endif()
        set(path ${${pkg}_SOURCE_DIR}/${file})
    else()
        set(path ${GGUI_TPL_DIR}/${file})
    endif()
    if(NOT EXISTS ${path})
        message(FATAL_ERROR "License file missing for ${label}: ${path}")
    endif()
    file(READ ${path} text)
    file(APPEND ${GGUI_TPL_FILE}
        "\n================================================================================\n"
        "${label} (${spdx})\n"
        "================================================================================\n\n"
        "${text}\n")
endfunction()

ggui_tpl_add("libgit2 (with its bundled zlib, xdiff, ntlmclient and others)" libgit2
    "GPL-2.0 with linking exception" COPYING)
ggui_tpl_add("CLI11" CLI11 BSD-3-Clause LICENSE)
ggui_tpl_add("spdlog (with its bundled fmt)" spdlog MIT LICENSE)
ggui_tpl_add("nlohmann/json" nlohmann_json MIT LICENSE.MIT)
ggui_tpl_add("miniz" miniz MIT LICENSE)
ggui_tpl_add("SDL3" SDL3 Zlib LICENSE.txt)
ggui_tpl_add("efsw" efsw MIT LICENSE)
ggui_tpl_add("nativefiledialog-extended" nfd Zlib LICENSE)
ggui_tpl_add("Dear ImGui" imgui MIT LICENSE.txt)
ggui_tpl_add("ImGuiColorTextEdit" ImGuiColorTextEdit MIT LICENSE)
ggui_tpl_add("IconFontCppHeaders" IconFontCppHeaders Zlib licence.txt)
ggui_tpl_add("Material Symbols font" "" Apache-2.0 MaterialSymbols-LICENSE.txt)
ggui_tpl_add("Noto Sans Mono font" "" OFL-1.1 NotoSansMono-OFL.txt)
ggui_tpl_add("JetBrains Mono font" "" OFL-1.1 JetBrainsMono-OFL.txt)

# The MinGW package links libwinpthread and the GCC runtime statically. The GCC Runtime Library
# Exception lets the program keep its own license, so only winpthreads needs a text.
if(MINGW)
    ggui_tpl_add("winpthreads (mingw-w64, linked statically)" "" MIT winpthreads-COPYING.txt)
endif()
