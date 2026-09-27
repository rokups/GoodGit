// Process environment helpers (portable setenv/unsetenv, pid, executable directory).
#pragma once

#include <string>

namespace ggui {

void setEnv(const std::string& name, const std::string& value);
void unsetEnv(const std::string& name);
std::string getEnv(const std::string& name);
long long processId();
// Directory containing the running executable (with a trailing separator removed).
std::string executableDir();

} // namespace ggui
