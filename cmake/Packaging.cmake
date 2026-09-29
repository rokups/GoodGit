# Install rules and CPack.
#
# ggui and git-gg are installed into the same directory, so putting that directory on PATH makes
# `git gg` resolve. Fonts are embedded in ggui (EmbedFont.cmake); there are no other runtime files.
#
#   Linux:   bin/ggui, bin/git-gg, share/applications/ggui.desktop,
#            share/icons/hicolor/256x256/apps/ggui.png, share/doc/ggui/LICENSE
#            packages: TGZ and ZIP (relocatable, one top directory), DEB (/usr)
#   Windows: ggui.exe, git-gg.exe, LICENSE.txt at the top of the ZIP
#
# Only the "ggui" install component is packaged: the fetched dependencies' own install rules
# (libgit2 headers, static libraries, CMake configs) go to "thirdparty" (CMakeLists.txt). Install
# by hand with `cmake --install build/release --component ggui --prefix PREFIX`.
#
# Packages are made from the release presets (no test engine), e.g.
#   cmake --workflow --preset package-linux
include(GNUInstallDirs)

if(WIN32)
    set(GGUI_INSTALL_BINDIR .)
    set(GGUI_INSTALL_DOCDIR .)
else()
    set(GGUI_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(GGUI_INSTALL_DOCDIR ${CMAKE_INSTALL_DOCDIR})
endif()

install(TARGETS git-gg RUNTIME DESTINATION ${GGUI_INSTALL_BINDIR} COMPONENT ggui)
if(GGUI_BUILD_APP)
    install(TARGETS ggui RUNTIME DESTINATION ${GGUI_INSTALL_BINDIR} COMPONENT ggui)
    if(UNIX AND NOT APPLE)
        install(FILES ${PROJECT_SOURCE_DIR}/res/ggui.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            COMPONENT ggui)
        install(FILES ${PROJECT_SOURCE_DIR}/res/ggui.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps COMPONENT ggui)
    endif()
endif()
if(WIN32)
    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${GGUI_INSTALL_DOCDIR} RENAME LICENSE.txt
        COMPONENT ggui)
else()
    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${GGUI_INSTALL_DOCDIR} COMPONENT ggui)
endif()

# MSVC: the presets link the static CRT (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded), so there is
# nothing to ship. A build with the DLL CRT gets the redistributable DLLs next to the executables.
if(MSVC AND (NOT CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${GGUI_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT ggui)
    set(CMAKE_INSTALL_UCRT_LIBRARIES ON)
    include(InstallRequiredSystemLibraries)
endif()

set(GGUI_PACKAGE_CONTACT "Rokas Kupstys <rokups@zoho.com>" CACHE STRING "Package maintainer (DEB)")

set(CPACK_PACKAGE_NAME ggui)
set(CPACK_PACKAGE_VENDOR "ggui")
set(CPACK_PACKAGE_CONTACT "${GGUI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Git GUI with first-class conflicts and undo")
set(CPACK_PACKAGE_DESCRIPTION
    "ggui is a desktop Git client with an undo journal, first-class (in-file) conflicts,\n"
    "in-memory history editing and an interactive rebase editor. It ships git-gg, the\n"
    "`git gg` subcommand (undo, redo, managed hooks, sequence editor).")
string(REPLACE ";" "" CPACK_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};ggui;/")
set(CPACK_STRIP_FILES ON)
set(CPACK_ARCHIVE_THREADS 0)

if(WIN32)
    if(MSVC)
        set(GGUI_PACKAGE_SYSTEM windows-x64-msvc)
    else()
        set(GGUI_PACKAGE_SYSTEM windows-x64-mingw)
    endif()
    set(CPACK_GENERATOR ZIP)
else()
    set(GGUI_PACKAGE_SYSTEM linux-${CMAKE_SYSTEM_PROCESSOR})
    set(CPACK_GENERATOR "TGZ;ZIP;DEB")
endif()
set(CPACK_PACKAGE_FILE_NAME ${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${GGUI_PACKAGE_SYSTEM})

if(NOT WIN32)
    # DEB (installs under /usr). dpkg-shlibdeps adds the linked libraries when it is available (the
    # Ubuntu CI job); the libraries SDL3 opens at run time (Vulkan, X11/Wayland) are listed by hand.
    # git is found on PATH at run time; 2.36 is the minimum ggui supports.
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION vcs)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
    endif()
    set(GGUI_DEB_DEPENDS "git (>= 1:2.36), libvulkan1, mesa-vulkan-drivers | vulkan-icd, libx11-6, libxext6")
    find_program(GGUI_DPKG_SHLIBDEPS dpkg-shlibdeps)
    if(GGUI_DPKG_SHLIBDEPS)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    else()
        # Without dpkg-shlibdeps: what ggui links (ldd), with this build host's glibc as the minimum
        # (binaries need at most the glibc they were built against).
        execute_process(COMMAND getconf GNU_LIBC_VERSION OUTPUT_VARIABLE _glibc OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        string(REGEX MATCH "[0-9]+\\.[0-9]+" _glibc "${_glibc}")
        if(_glibc)
            set(GGUI_DEB_DEPENDS "libc6 (>= ${_glibc}), libdbus-1-3, ${GGUI_DEB_DEPENDS}")
        else()
            set(GGUI_DEB_DEPENDS "libc6, libdbus-1-3, ${GGUI_DEB_DEPENDS}")
        endif()
        if(NOT GGUI_STATIC_CXX_RUNTIME)
            set(GGUI_DEB_DEPENDS "${GGUI_DEB_DEPENDS}, libstdc++6, libgcc-s1")
        endif()
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${GGUI_DEB_DEPENDS}")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
        "libwayland-client0, libwayland-cursor0, libwayland-egl1, libxkbcommon0, libdecor-0-0, libxcursor1, libxi6, libxrandr2, libxfixes3, libxss1, xdg-desktop-portal")
endif()

include(CPack)
