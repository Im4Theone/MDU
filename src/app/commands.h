#pragma once

#include "settings/paths.h"

#include <chrono>

// One-shot command-line operations used by the installer. They run without
// starting the application and never show UI.
namespace mdu::commands {

// Asks a running MDU in this session to exit cleanly and waits for it.
// Returns true if none is running or it exited in time.
bool stopRunningInstance(std::chrono::milliseconds timeout);

// --set-startup on|off: stores the preference and updates the Run key.
int setStartup(const AppPaths& paths, bool enabled);

// --uninstall-cleanup: stops MDU, restores the wallpaper it replaced, removes
// the startup registration and deletes caches, logs and WebView2 data.
// The settings file is left alone; the uninstaller asks the user about it.
int uninstallCleanup(const AppPaths& paths);

}  // namespace mdu::commands
