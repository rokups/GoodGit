// spdlog setup: env-driven levels (SPDLOG_LEVEL) and an optional GGUI_LOG_FILE sink.
// A small in-memory ring keeps recent lines for test failure reports.
#pragma once

#include <string>
#include <vector>

namespace ggui {

void initLogging();
// Most recent log lines (newest last), for test failure output.
std::vector<std::string> recentLogLines();
void clearRecentLogLines();

} // namespace ggui
