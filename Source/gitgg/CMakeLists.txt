# git-gg: the `git gg` subcommand (CLI11, no SDL/ImGui).
file(GLOB GITGG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(git-gg ${GITGG_SOURCES})
target_link_libraries(git-gg PRIVATE libgg CLI11::CLI11)
ggui_first_party(git-gg)
if(MINGW)
    target_link_options(git-gg PRIVATE -static)
endif()
