#pragma once

#include "app/crash_guard.h"
#include "dock/dock_window.h"
#include "settings/config.h"
#include "settings/paths.h"
#include "ui/settings_protocol.h"
#include "ui/settings_window.h"
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
    std::filesystem::path dataDirectory;   // --data-dir: use instead of %LOCALAPPDATA%\MDU
    std::filesystem::path selfTestReport;  // --self-test: run checks, write this report, exit
};

class App {
public:
    static constexpr wchar_t messageWindowClass[] = L"MDU.MessageWindow";
    static constexpr UINT secondInstanceMessage = WM_APP + 10;
    static constexpr UINT exitRequestMessage = WM_APP + 11;  // sent by `mdu.exe --exit` (installer)

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
    void onDockMessage(DockWindow& dock, const bridge::IncomingMessage& message);
    void onWebViewProcessFailed();

    // Wallpaper.
    void applyWallpaperSetting();

    // Settings window (app_settings.cpp).
    void openSettings();
    void onSettingsMessage(const std::string& text);
    void sendSettingsState(const std::string& notice = {});
    std::vector<settings_protocol::ThemeOption> availableThemes() const;
    void openSettingsFile();
    // Saves and applies a complete new configuration, doing only the work
    // that the changed values require.
    void applyConfig(Config next);

    // Self-test (app_self_test.cpp). Runs the real application, exercises it
    // through its public surfaces and writes a JSON report.
    struct SelfTestCheck {
        std::string name;
        bool passed = false;
        std::string detail;
    };
    struct SelfTestState {
        enum class Stage {
            WaitingForDocks,
            Settling,
            Probing,
            SettingsOpening,
            SettingsChanged,
            SettingsReverted,
            SettingsClosing,
            Done,
        };
        Stage stage = Stage::WaitingForDocks;
        std::vector<SelfTestCheck> checks;
        std::string probeItemId;
        std::vector<std::string> launchedIds;
        int failedLaunches = 0;
        int unknownLaunchRequests = 0;
        bool wallpaperAttempted = false;
        bool wallpaperApplied = false;
        std::string wallpaperError;
        std::string wallpaperImageBeforeSettings;
        int exitCode = 1;
    };
    void startSelfTest();
    void selfTestOnDockReady();
    void selfTestStep();
    void selfTestCheckDocks();
    void selfTestProbe();
    void selfTestCheckWallpaper();
    void selfTestOnSettingsReady();
    void selfTestCheckSettingsChange(DockEdge expected, const std::string& label);
    void finishSelfTest(bool timedOut);
    void selfTestScreenshot(const std::string& name, bool dockPixelChecks = true);
    // Private window messages shared by app.cpp, app_settings.cpp and app_self_test.cpp.
    static constexpr UINT webViewFailedMessage = WM_APP + 3;
    static constexpr UINT settingsClosedMessage = WM_APP + 4;
    static constexpr UINT_PTR selfTestTimeoutTimer = 2;
    static constexpr UINT_PTR selfTestStepTimer = 3;
    bool selfTestActive() const { return selfTest_ && selfTest_->stage != SelfTestState::Stage::Done; }

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
    std::unique_ptr<SelfTestState> selfTest_;
    std::unique_ptr<SettingsWindow> settings_;
    int rejectedSettingsMessages_ = 0;
};

}  // namespace mdu
