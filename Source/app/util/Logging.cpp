#include "util/Logging.hpp"

#include "shell/Settings.hpp"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>

namespace ggui {
namespace {

class RingSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::vector<std::string> lines()
    {
        std::lock_guard lock(mutex_);
        return {m_lines.begin(), m_lines.end()};
    }
    void clear()
    {
        std::lock_guard lock(mutex_);
        m_lines.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::string line(formatted.data(), formatted.size());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        m_lines.push_back(std::move(line));
        if (m_lines.size() > 2000)
            m_lines.pop_front();
    }
    void flush_() override { }

private:
    std::deque<std::string> m_lines;
};

std::shared_ptr<RingSink> g_ring;
// A sink that is always in the logger. Its inner sink (the rotated file) changes under the sink mutex,
// so the logger's sink list never changes while other threads log.
class SwitchSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    void setInner(std::shared_ptr<spdlog::sinks::sink> inner)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m_inner)
            m_inner->flush();
        m_inner = std::move(inner);
    }
    void setInnerLevel(spdlog::level::level_enum level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m_inner)
            m_inner->set_level(level);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (m_inner && m_inner->should_log(msg.level))
            m_inner->log(msg);
    }
    void flush_() override
    {
        if (m_inner)
            m_inner->flush();
    }

private:
    std::shared_ptr<spdlog::sinks::sink> m_inner;
};

std::shared_ptr<SwitchSink> g_switch; // in the default logger after initLogging
bool g_fileOpen = false;
spdlog::level::level_enum g_fileLevel = spdlog::level::debug;
bool g_envLevel = false; // SPDLOG_LEVEL is set: it decides the logger level, not the setting

constexpr int kKeptLogFiles = 6; // the current file and 5 old files

} // namespace

std::filesystem::path logDirectory() { return Settings::prefDir() / "logs"; }

void openLogFile()
{
    if (g_fileOpen || !g_switch)
        return;
    const auto logger = spdlog::default_logger();
    if (!logger)
        return;
    try {
        const std::filesystem::path dir = logDirectory();
        std::filesystem::create_directories(dir);
        // The sink inserts the date: ggui_YYYY-MM-DD.log. It rotates at midnight and deletes the oldest file.
        auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>((dir / "ggui.log").string(), 0, 0, false, kKeptLogFiles);
        sink->set_level(g_fileLevel);
        g_switch->setInner(std::move(sink));
        g_fileOpen = true;
    } catch (const std::exception& e) {
        logger->warn("cannot open the log file in {}: {}", logDirectory().string(), e.what());
    }
}

void closeLogFile()
{
    if (!g_fileOpen)
        return;
    if (const auto logger = spdlog::default_logger())
        logger->flush();
    g_switch->setInner(nullptr);
    g_fileOpen = false;
}

void setLogLevel(bool trace)
{
    g_fileLevel = trace ? spdlog::level::trace : spdlog::level::debug;
    if (g_switch)
        g_switch->setInnerLevel(g_fileLevel);
    if (!g_envLevel)
        if (const auto logger = spdlog::default_logger())
            logger->set_level(g_fileLevel);
}

void initLogging(bool withLogFile)
{
    g_switch.reset();
    g_fileOpen = false;
    g_fileLevel = spdlog::level::debug;
    std::vector<spdlog::sink_ptr> sinks;
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console->set_level(spdlog::level::warn);
    sinks.push_back(console);
    if (const char* file = std::getenv("GGUI_LOG_FILE"); file && *file) {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        fileSink->set_level(spdlog::level::trace);
        sinks.push_back(fileSink);
    }
    g_switch = std::make_shared<SwitchSink>();
    sinks.push_back(g_switch);
    g_ring = std::make_shared<RingSink>();
    g_ring->set_level(spdlog::level::debug);
    sinks.push_back(g_ring);
    auto logger = std::make_shared<spdlog::logger>("ggui", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%t] [%l] %v");
    if (withLogFile)
        openLogFile();
    // SPDLOG_LEVEL=debug (or "info,ggui=trace") overrides the default level.
    spdlog::cfg::load_env_levels();
    if (const char* level = std::getenv("SPDLOG_LEVEL"); level && *level) {
        g_envLevel = true;
        console->set_level(spdlog::level::trace);
    }
}

std::vector<std::string> recentLogLines() { return g_ring ? g_ring->lines() : std::vector<std::string>{}; }

void clearRecentLogLines()
{
    if (g_ring)
        g_ring->clear();
}

} // namespace ggui
