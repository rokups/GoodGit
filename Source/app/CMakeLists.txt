# ggui: the desktop app. Integration tests are compiled in behind GGUI_ENABLE_IMGUI_TEST_ENGINE.
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(GGUI_ENABLE_IMGUI_TEST_ENGINE)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/Source/tests/*.cpp)
    list(APPEND APP_SOURCES ${TEST_SOURCES})
endif()
add_executable(ggui ${APP_SOURCES})
target_include_directories(ggui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/Source)
target_link_libraries(ggui PRIVATE ggui_core imgui imgui_color_text_edit icon_font_headers
    nfd nlohmann_json::nlohmann_json spdlog::spdlog)
target_compile_definitions(ggui PRIVATE GGUI_VERSION="${PROJECT_VERSION}")
if(WIN32 AND GGUI_ENABLE_IMGUI_TEST_ENGINE)
    target_link_libraries(ggui PRIVATE dbghelp) # the test runner's crash report
endif()
set_target_properties(ggui PROPERTIES WIN32_EXECUTABLE $<NOT:$<BOOL:${GGUI_ENABLE_IMGUI_TEST_ENGINE}>>)
ggui_embed_font(ggui ggui_font_material_symbols ${GGUI_FONT_DIR}/MaterialSymbolsOutlined.ttf)
ggui_embed_font(ggui ggui_font_noto_sans_mono ${GGUI_FONT_DIR}/NotoSansMono.ttf)
ggui_embed_font(ggui ggui_font_jetbrains_mono ${GGUI_FONT_DIR}/JetBrainsMono.ttf)
ggui_first_party(ggui)
ggui_executable(ggui "ggui - Git GUI")
if(MSVC)
    # Plain main() also for the WIN32 (GUI) subsystem.
    target_link_options(ggui PRIVATE /ENTRY:mainCRTStartup)
endif()
# git-gg is built next to ggui so tests can put it on PATH.
add_dependencies(ggui git-gg)
