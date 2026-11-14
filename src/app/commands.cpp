#include "app/commands.h"

#include "app/app.h"
#include "settings/config.h"
#include "utilities/log.h"
#include "wallpaper/wallpaper.h"
#include "windows/startup.h"

#include <windows.h>

#include <filesystem>
#include <thread>

namespace mdu::commands {

namespace {

class ProcessHandle {
public:
    explicit ProcessHandle(HANDLE handle) : handle_(handle) {}
    ~ProcessHandle()
    {
        if (handle_) {
            CloseHandle(handle_);
        }
    }
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;
    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

// WebView2 helper processes can hold files for a moment after MDU exits.
bool removeWithRetries(const std::filesystem::path& path)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (!ec && !std::filesystem::exists(path, ec)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}

}  // namespace

bool stopRunningInstance(std::chrono::milliseconds timeout)
{
    const HWND window = FindWindowW(App::messageWindowClass, nullptr);
    if (!window) {
        return true;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    const ProcessHandle process(OpenProcess(SYNCHRONIZE, FALSE, processId));

    PostMessageW(window, App::exitRequestMessage, 0, 0);
    if (!process.get()) {
        return false;
    }
    return WaitForSingleObject(process.get(), static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
}

int setStartup(const AppPaths& paths, bool enabled)
{
    auto loaded = loadConfigFile(paths.configFile);
    // Never overwrite a settings file the user has to fix by hand.
    if (!loaded.fileWasInvalid) {
        loaded.config.startWithWindows = enabled;
        std::string error;
        if (!saveConfigFile(paths.configFile, loaded.config, error)) {
            return 1;
        }
    }
    return (enabled ? startup::registerExecutable(executablePath()) : startup::unregister()) ? 0 : 1;
}

int uninstallCleanup(const AppPaths& paths)
{
    bool ok = stopRunningInstance(std::chrono::seconds(15));

    // Restoring the wallpaper does not decode images, so no WIC factory is needed.
    wallpaper::Manager wallpaper(paths.cacheDirectory, paths.wallpaperRestoreFile, nullptr);
    std::string error;
    ok = wallpaper.restore(error) && ok;
    ok = startup::unregister() && ok;

    for (const auto& directory : {paths.cacheDirectory, paths.webViewDataDirectory, paths.logFile.parent_path(),
                                  paths.crashStateFile.parent_path()}) {
        ok = removeWithRetries(directory) && ok;
    }
    return ok ? 0 : 1;
}

}  // namespace mdu::commands
