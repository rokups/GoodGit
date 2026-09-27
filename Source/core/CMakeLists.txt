# ggui_core: the repository engine (worker threads, snapshots, history, diff, blame, rewrites).
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(ggui_core STATIC ${CORE_SOURCES})
target_include_directories(ggui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ggui_core PUBLIC libgg efsw spdlog::spdlog)
ggui_first_party(ggui_core)
