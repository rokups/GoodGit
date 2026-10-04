// Process environment helpers (portable setenv/unsetenv, pid, executable directory and path).
#pragma once

#include <string>

namespace ggui {

void setEnv(const std::string& name, const std::string& value);
void unsetEnv(const std::string& name);
std::string getEnv(const std::string& name);
long long processId();
// Directory containing the running executable (with a trailing separator removed).
std::string executableDir();
// Full path of the running executable ("" when it cannot be told, and off Linux).
std::string executablePath();

} // namespace ggui
