#include "util/Logging.hpp"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <deque>
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

} // namespace

void initLogging()
{
    std::vector<spdlog::sink_ptr> sinks;
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console->set_level(spdlog::level::warn);
    sinks.push_back(console);
    if (const char* file = std::getenv("GGUI_LOG_FILE"); file && *file) {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        fileSink->set_level(spdlog::level::trace);
        sinks.push_back(fileSink);
    }
    g_ring = std::make_shared<RingSink>();
    g_ring->set_level(spdlog::level::debug);
    sinks.push_back(g_ring);
    auto logger = std::make_shared<spdlog::logger>("ggui", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%H:%M:%S.%e] [%t] [%l] %v");
    // SPDLOG_LEVEL=debug (or "info,ggui=trace") overrides the default level.
    spdlog::cfg::load_env_levels();
    if (const char* level = std::getenv("SPDLOG_LEVEL"); level && *level)
        console->set_level(spdlog::level::trace);
}

std::vector<std::string> recentLogLines() { return g_ring ? g_ring->lines() : std::vector<std::string>{}; }

void clearRecentLogLines()
{
    if (g_ring)
        g_ring->clear();
}

} // namespace ggui
