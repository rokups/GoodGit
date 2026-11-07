// spdlog setup: env-driven levels (SPDLOG_LEVEL), an optional GGUI_LOG_FILE sink and a rotated
// log file in logDirectory().
// A small in-memory ring keeps recent lines for test failure reports.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ggui {

// `withLogFile` false: the rotated file is not opened (tests open it with openLogFile after they set GGUI_PREF_PATH).
void initLogging(bool withLogFile = true);
// The directory of the rotated log files: <settings directory>/logs.
std::filesystem::path logDirectory();
// Opens the rotated file ggui_<date>.log in logDirectory(): it rotates at midnight and keeps 6 files.
// When the file cannot be opened, one warning goes to the log and the application continues.
void openLogFile();
// Closes the rotated file. Does nothing when it is not open.
void closeLogFile();
// The log level of the logger and of the rotated file: trace, or debug. The console stays at warn.
// SPDLOG_LEVEL in the environment keeps the logger level.
void setLogLevel(bool trace);
// Most recent log lines (newest last), for test failure output.
std::vector<std::string> recentLogLines();
void clearRecentLogLines();

} // namespace ggui
