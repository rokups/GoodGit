#include "util/Env.hpp"

#include <SDL3/SDL_filesystem.h>

#include <cstdlib>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ggui {

void setEnv(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    ::setenv(name.c_str(), value.c_str(), 1);
#endif
}

void unsetEnv(const std::string& name)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), "");
#else
    ::unsetenv(name.c_str());
#endif
}

std::string getEnv(const std::string& name)
{
    const char* v = std::getenv(name.c_str());
    return v ? v : "";
}

long long processId()
{
#ifdef _WIN32
    return static_cast<long long>(GetCurrentProcessId());
#else
    return static_cast<long long>(getpid());
#endif
}

std::string executableDir()
{
    std::string dir = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    return dir;
}

} // namespace ggui
