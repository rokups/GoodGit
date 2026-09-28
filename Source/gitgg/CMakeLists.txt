# git-gg: the `git gg` subcommand (CLI11, no SDL/ImGui).
file(GLOB GITGG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(git-gg ${GITGG_SOURCES})
target_link_libraries(git-gg PRIVATE libgg CLI11::CLI11)
target_compile_definitions(git-gg PRIVATE GGUI_VERSION="${PROJECT_VERSION}")
ggui_first_party(git-gg)
ggui_executable(git-gg "git gg - undo, first-class conflicts and helpers for ggui")
