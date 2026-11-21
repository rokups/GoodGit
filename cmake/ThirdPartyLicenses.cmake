# Writes ${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES.txt from the fetched CPM sources, so the texts
# follow the pinned versions. Components that this build does not fetch are skipped.
# Test-only packages (ImGui test engine, stb) are not listed: release builds do not ship them.
set(GGUI_TPL_FILE ${CMAKE_BINARY_DIR}/THIRD_PARTY_LICENSES.txt)
set(GGUI_TPL_DIR ${CMAKE_CURRENT_LIST_DIR}/licenses)
file(WRITE ${GGUI_TPL_FILE}
    "Third-party components in ggui and git-gg. The ggui license is in LICENSE.\n")

# ggui_tpl_add(<label> <version> <package or ""> <license id> <license file>)
# Keep each <version> equal to the pin in cmake/Dependencies.cmake (CPM does not expose the tag).
# A package license file is relative to the CPM source; otherwise it is in cmake/licenses.
function(ggui_tpl_add label version pkg spdx file)
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
        "${label} ${version} (${spdx})\n"
        "================================================================================\n\n"
        "${text}\n")
endfunction()

ggui_tpl_add("libgit2 (with its bundled zlib, xdiff, ntlmclient and others)" "v1.9.6" libgit2
    "GPL-2.0 with linking exception" COPYING)
ggui_tpl_add("CLI11" "v2.6.2" CLI11 BSD-3-Clause LICENSE)
ggui_tpl_add("spdlog (with its bundled fmt)" "v1.17.0" spdlog MIT LICENSE)
ggui_tpl_add("nlohmann/json" "v3.12.0" nlohmann_json MIT LICENSE.MIT)
ggui_tpl_add("miniz" "3.1.2" miniz MIT LICENSE)
ggui_tpl_add("SDL3" "release-3.4.4" SDL3 Zlib LICENSE.txt)
ggui_tpl_add("efsw" "1.6.3" efsw MIT LICENSE)
ggui_tpl_add("nativefiledialog-extended" "v1.3.0" nfd Zlib LICENSE)
ggui_tpl_add("Dear ImGui" "commit 84a9d532b6f6" imgui MIT LICENSE.txt)
ggui_tpl_add("ImGuiColorTextEdit" "commit fa6fd434d3db" ImGuiColorTextEdit MIT LICENSE)
ggui_tpl_add("IconFontCppHeaders" "commit 4577f2f72c36" IconFontCppHeaders Zlib licence.txt)
ggui_tpl_add("Material Symbols font" "commit bd8cb85bd4ba" "" Apache-2.0 MaterialSymbols-LICENSE.txt)
ggui_tpl_add("Noto Sans Mono font" "commit 23e54b51ddff" "" OFL-1.1 NotoSansMono-OFL.txt)
ggui_tpl_add("JetBrains Mono font" "v2.304" "" OFL-1.1 JetBrainsMono-OFL.txt)
