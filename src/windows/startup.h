#pragma once

#include <filesystem>

// Per-user "start after sign-in" registration via
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run. No administrator rights
// are needed, and Windows' own Startup Apps setting can still disable it.
namespace mdu::startup {

bool isRegistered();
bool registerExecutable(const std::filesystem::path& executable);
bool unregister();

}  // namespace mdu::startup
