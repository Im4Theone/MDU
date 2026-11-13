#pragma once

#include "app/crash_guard.h"
#include "dock/dock_window.h"
#include "settings/config.h"
#include "settings/paths.h"
#include "ui/theme.h"
#include "ui/webview_environment.h"
#include "wallpaper/wallpaper.h"
#include "windows/tray_icon.h"

#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mdu {

struct LaunchOptions {
    bool safeMode = false;      // --safe-mode or Shift held at start
    bool fromStartup = false;   // launched by the Run key after sign-in
};

class App {
public:
    static constexpr wchar_t messageWindowClass[] = L"MDU.MessageWindow";
    static constexpr UINT secondInstanceMessage = WM_APP + 10;

    App(HINSTANCE instance, AppPaths paths, LaunchOptions options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop. Returns the process exit code.
    int run();

    // Set when the user chose "Restart in normal mode"; main() relaunches after shutdown.
    bool restartRequested() const { return restartRequested_; }

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool initialize();
    void shutdown();

    // Configuration and theme.
    void loadConfiguration();
    void saveConfiguration();
    void loadActiveTheme();
    void applyStartupRegistration();
    void reloadEverything();

    // Docks.
    void scheduleDockRebuild();
    void rebuildDocks();
    void layoutDock(DockWindow& dock);
    void refreshIcons();
    void sendState(DockWindow& dock);
    void onDockMessage(DockWindow& dock, const bridge::IncomingMessage& message);
    void onWebViewProcessFailed();

    // Wallpaper.
    void applyWallpaperSetting();

    // Tray.
    void showTrayMenu(POINT anchor);
    void handleTrayCommand(UINT command);
    void notify(const std::wstring& title, const std::wstring& text);

    HINSTANCE instance_;
    AppPaths paths_;
    LaunchOptions options_;
    CrashGuard crashGuard_;
    bool safeMode_ = false;
    bool restartRequested_ = false;

    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    HICON icon_ = nullptr;
    std::unique_ptr<TrayIcon> tray_;

    Config config_;
    std::optional<Theme> theme_;

    Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory_;
    std::unique_ptr<wallpaper::Manager> wallpaper_;

    WebViewEnvironment webViewEnvironment_;
    std::vector<std::unique_ptr<DockWindow>> docks_;
    std::map<std::string, std::string> iconCache_;  // item id -> data URL
    std::vector<std::chrono::steady_clock::time_point> browserFailures_;
};

}  // namespace mdu
