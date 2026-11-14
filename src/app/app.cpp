#include "app/app.h"

#include "dock/dock_layout.h"
#include "utilities/log.h"
#include "utilities/strings.h"
#include "windows/app_launcher.h"
#include "windows/icon_extractor.h"
#include "windows/monitors.h"
#include "windows/startup.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <format>

namespace mdu {

namespace {

constexpr UINT trayMessage = WM_APP + 1;
constexpr UINT applyWallpaperMessage = WM_APP + 2;
constexpr UINT webViewFailedMessage = WM_APP + 3;
constexpr UINT_PTR rebuildTimer = 1;
constexpr UINT rebuildDelayMs = 300;  // display changes arrive in bursts


std::string timestampForFileName()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y%m%d-%H%M%S}", now);
}

Rect currentWorkArea(const MonitorInfo& monitor)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor.handle, &info)) {
        return toRect(info.rcWork);
    }
    return monitor.workArea;
}

}  // namespace

App::App(HINSTANCE instance, AppPaths paths, LaunchOptions options)
    : instance_(instance),
      paths_(std::move(paths)),
      options_(options),
      crashGuard_(paths_.crashStateFile, CrashGuard::Policy{})
{
}

App::~App()
{
    shutdown();
}

int App::run()
{
    if (!initialize()) {
        shutdown();
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    shutdown();
    return selfTest_ ? selfTest_->exitCode : static_cast<int>(message.wParam);
}

bool App::initialize()
{
    log::open(paths_.logFile);
    log::info(std::format("MDU {} starting", MDU_VERSION));

    const auto guard = crashGuard_.onStartup(std::chrono::system_clock::now());
    if (guard.previousRunCrashed) {
        log::warning(std::format("previous session ended unexpectedly ({} recent)", guard.recentCrashes));
    }
    safeMode_ = options_.safeMode || guard.safeModeRecommended;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &App::windowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = messageWindowClass;
    RegisterClassExW(&windowClass);

    // A hidden top-level window (not HWND_MESSAGE) so it receives broadcasts
    // such as WM_DISPLAYCHANGE, WM_SETTINGCHANGE and TaskbarCreated.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, messageWindowClass, L"MDU", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              instance_, this);
    if (!window_) {
        log::error(std::format("cannot create message window: error {}", GetLastError()));
        return false;
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (FAILED(LoadIconMetric(instance_, MAKEINTRESOURCEW(1), LIM_SMALL, &icon_))) {
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
    tray_ = std::make_unique<TrayIcon>(window_, trayMessage, icon_, L"MDU");
    if (!tray_->add()) {
        // Explorer may not be ready yet right after sign-in; TaskbarCreated retries.
        log::warning("tray icon not added yet");
    }

    loadConfiguration();
    if (!options_.selfTestReport.empty()) {
        selfTest_ = std::make_unique<SelfTestState>();  // never touches the startup registration
    } else {
        applyStartupRegistration();
    }

    if (safeMode_) {
        log::warning("running in safe mode");
        notify(L"MDU is in safe mode",
               options_.safeMode ? L"The dock and wallpaper are off. Use the tray icon to restart normally."
                                 : L"MDU closed unexpectedly several times, so the dock and wallpaper are off. "
                                   L"Use the tray icon to restart normally.");
        if (selfTest_) {
            selfTest_->checks.push_back({"normal_mode", false, "started in safe mode"});
            finishSelfTest(false);
        }
        return true;
    }

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imagingFactory_));
    if (imagingFactory_) {
        wallpaper_ = std::make_unique<wallpaper::Manager>(paths_.cacheDirectory, paths_.wallpaperRestoreFile,
                                                          imagingFactory_.Get());
    } else {
        log::error("WIC is not available; icons and wallpaper are disabled");
    }

    loadActiveTheme();
    refreshIcons();
    webViewEnvironment_.create(paths_.webViewDataDirectory);
    rebuildDocks();

    // Wallpaper work can take a moment the first time, so do it after the
    // docks are on their way.
    PostMessageW(window_, applyWallpaperMessage, 0, 0);
    if (selfTest_) {
        startSelfTest();
    }
    return true;
}

void App::shutdown()
{
    KillTimer(window_, rebuildTimer);
    docks_.clear();  // removes app bar reservations
    webViewEnvironment_.reset();
    tray_.reset();
    if (window_) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    crashGuard_.onCleanExit();
    log::close();
}

LRESULT CALLBACK App::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        // CreateWindowExW has not returned yet, so window_ is still null; the
        // creation messages must already be handled with the real handle.
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
        return app->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        // Explorer restarted: the tray icon and app bar registrations are gone.
        tray_->add();
        for (auto& dock : docks_) {
            dock->appBar().unregister();
            dock->appBar().registerBar();
        }
        scheduleDockRebuild();
        return 0;
    }

    switch (message) {
    case trayMessage:
        switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            showTrayMenu(POINT{static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam))});
            break;
        default:
            break;
        }
        return 0;

    case secondInstanceMessage:
        notify(L"MDU is already running", L"Use the MDU icon in the notification area to change settings or exit.");
        return 0;

    case applyWallpaperMessage:
        applyWallpaperSetting();
        return 0;

    case webViewFailedMessage:
        onWebViewProcessFailed();
        return 0;

    case WM_DISPLAYCHANGE:
        scheduleDockRebuild();
        PostMessageW(window_, applyWallpaperMessage, 0, 0);
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA && config_.dock.placement == DockPlacement::Overlay) {
            scheduleDockRebuild();
        }
        return 0;

    case WM_TIMER:
        if (wParam == rebuildTimer) {
            KillTimer(window_, rebuildTimer);
            rebuildDocks();
        } else if (wParam == selfTestTimeoutTimer) {
            finishSelfTest(true);
        } else if (wParam == selfTestStepTimer) {
            KillTimer(window_, selfTestStepTimer);
            selfTestStep();
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            // Windows is signing out or shutting down: this is a clean exit
            // even though the process may be terminated before run() returns.
            crashGuard_.onCleanExit();
            log::info("session ending");
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY: {
        const HWND window = window_;
        window_ = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }

    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

// --- Configuration ------------------------------------------------------------

void App::loadConfiguration()
{
    auto result = loadConfigFile(paths_.configFile);
    for (const auto& warning : result.warnings) {
        log::warning("config: " + warning);
    }

    std::error_code ec;
    if (result.fileWasInvalid) {
        // Keep the user's file before anything can overwrite it.
        auto backup = paths_.configFile;
        backup += L".invalid-" + toWide(timestampForFileName());
        std::filesystem::copy_file(paths_.configFile, backup, std::filesystem::copy_options::overwrite_existing, ec);
        notify(L"Settings file is invalid",
               L"MDU is using default settings. Your file was kept as " + backup.filename().wstring() + L".");
    }

    config_ = std::move(result.config);
    if (!std::filesystem::exists(paths_.configFile, ec)) {
        saveConfiguration();  // create a file the user can find and edit
    }
}

void App::saveConfiguration()
{
    std::string error;
    if (!saveConfigFile(paths_.configFile, config_, error)) {
        log::error("cannot save configuration: " + error);
        notify(L"Settings not saved", toWide(error));
    }
}

void App::loadActiveTheme()
{
    auto result = loadTheme(paths_.themesDirectory, config_.theme);
    if (!result.theme && config_.theme != "miku") {
        log::warning("theme \"" + config_.theme + "\" not available; using \"miku\"");
        result = loadTheme(paths_.themesDirectory, "miku");
    }
    for (const auto& warning : result.warnings) {
        log::warning("theme: " + warning);
    }
    theme_ = std::move(result.theme);
    if (!theme_) {
        log::error("no theme could be loaded; using built-in defaults");
    }
}

void App::applyStartupRegistration()
{
    const bool ok = config_.startWithWindows ? startup::registerExecutable(executablePath()) : startup::unregister();
    if (!ok) {
        log::error("cannot update startup registration");
    }
}

void App::reloadEverything()
{
    log::info("reloading configuration");
    loadConfiguration();
    applyStartupRegistration();
    if (safeMode_) {
        return;
    }
    loadActiveTheme();
    refreshIcons();
    rebuildDocks();
    applyWallpaperSetting();
}

// --- Docks ------------------------------------------------------------------

void App::scheduleDockRebuild()
{
    SetTimer(window_, rebuildTimer, rebuildDelayMs, nullptr);
}

void App::rebuildDocks()
{
    if (safeMode_) {
        return;
    }

    std::vector<MonitorInfo> targets;
    if (config_.dock.enabled) {
        for (const auto& monitor : enumerateMonitors()) {
            if (config_.dock.monitors == DockMonitors::All || monitor.primary) {
                targets.push_back(monitor);
            }
        }
    }

    std::erase_if(docks_, [&](const std::unique_ptr<DockWindow>& dock) {
        return std::ranges::none_of(
            targets, [&](const MonitorInfo& m) { return m.deviceName == dock->monitor().deviceName; });
    });

    for (const auto& monitor : targets) {
        const auto existing = std::ranges::find_if(
            docks_, [&](const auto& dock) { return dock->monitor().deviceName == monitor.deviceName; });
        if (existing != docks_.end()) {
            (*existing)->updateMonitor(monitor);
            continue;
        }

        DockWindow::Callbacks callbacks{
            .message = [this](DockWindow& dock, const bridge::IncomingMessage& message) { onDockMessage(dock, message); },
            .layoutInvalidated = [this] { scheduleDockRebuild(); },
            // Deferred: the dock that reports the failure is destroyed while handling it.
            .processFailed = [this](COREWEBVIEW2_PROCESS_FAILED_KIND) { PostMessageW(window_, webViewFailedMessage, 0, 0); },
        };
        auto dock = DockWindow::create(instance_, monitor, std::move(callbacks),
                                       {paths_.webDirectory, paths_.themesDirectory});
        if (!dock) {
            continue;
        }

        const std::wstring deviceName = monitor.deviceName;
        webViewEnvironment_.whenReady([this, deviceName](ICoreWebView2Environment* environment) {
            if (!environment) {
                static bool reported = false;
                if (!reported) {
                    reported = true;
                    notify(L"Dock unavailable", L"The Microsoft Edge WebView2 Runtime could not be started. "
                                                L"Install or repair it from Microsoft, then restart MDU.");
                }
                return;
            }
            for (auto& candidate : docks_) {
                if (candidate->monitor().deviceName == deviceName) {
                    candidate->attachWebView(environment);
                }
            }
        });
        docks_.push_back(std::move(dock));
    }

    for (auto& dock : docks_) {
        layoutDock(*dock);
    }
}

void App::layoutDock(DockWindow& dock)
{
    const auto& monitor = dock.monitor();
    const auto& settings = config_.dock;

    Rect area;
    if (settings.placement == DockPlacement::Reserve) {
        const int thickness = dockReservedThickness(monitor.dpi, settings.iconSize, settings.margin);
        area = dock.appBar().reserve(settings.edge, monitor.bounds, thickness);
        // Stay clear of a taskbar on a perpendicular edge.
        const Rect work = currentWorkArea(monitor);
        if (settings.edge == DockEdge::Bottom) {
            area.left = std::max(area.left, work.left);
            area.right = std::min(area.right, work.right);
        } else {
            area.top = std::max(area.top, work.top);
            area.bottom = std::min(area.bottom, work.bottom);
        }
        if (area.width() <= 0 || area.height() <= 0) {
            area = work;
        }
    } else {
        dock.appBar().releaseReservation();
        area = currentWorkArea(monitor);
    }

    const auto layout = computeDockLayout({
        .area = area,
        .dpi = monitor.dpi,
        .edge = settings.edge,
        .iconSize = settings.iconSize,
        .margin = settings.margin,
        .itemCount = static_cast<int>(config_.pinned.size()),
    });
    dock.setBounds(layout.window);

    const auto metrics = dockMetricsFor(layout.iconSize);
    bridge::DockViewState state;
    state.edge = settings.edge;
    state.iconSize = metrics.iconSize;
    state.padding = metrics.padding;
    state.gap = metrics.gap;
    state.shadowInset = metrics.shadowInset;
    state.motion = config_.motion;
    if (theme_) {
        state.themeVariables = theme_->variables;
        if (!theme_->stylesheet.empty()) {
            auto relative = theme_->stylesheet;
            std::ranges::replace(relative, '\\', '/');
            state.themeStylesheet = toUtf8(DockWindow::themesOrigin) + theme_->id + "/" + relative;
        }
    }
    for (const auto& item : config_.pinned) {
        const auto icon = iconCache_.find(item.id);
        state.items.push_back({item.id, item.name, icon != iconCache_.end() ? icon->second : std::string()});
    }
    dock.postState(bridge::buildStateMessage(state));
}

void App::refreshIcons()
{
    iconCache_.clear();
    if (!imagingFactory_) {
        return;
    }

    unsigned maxDpi = 96;
    for (const auto& monitor : enumerateMonitors()) {
        maxDpi = std::max(maxDpi, monitor.dpi);
    }
    const int size = std::clamp(dipsToPixels(config_.dock.iconSize, maxDpi), 32, 256);

    for (const auto& item : config_.pinned) {
        auto icon = extractIconDataUrl(imagingFactory_.Get(), expandEnvironment(toWide(item.target)), size);
        if (icon.empty()) {
            log::info("no shell icon for \"" + item.id + "\"; the dock shows a fallback");
        }
        iconCache_[item.id] = std::move(icon);
    }
}

void App::onDockMessage(DockWindow&, const bridge::IncomingMessage& message)
{
    if (std::holds_alternative<bridge::ReadyMessage>(message)) {
        if (selfTestActive()) {
            selfTestOnDockReady();
        }
        return;
    }

    if (const auto* launch = std::get_if<bridge::LaunchMessage>(&message)) {
        const auto item = std::ranges::find_if(config_.pinned, [&](const auto& p) { return p.id == launch->itemId; });
        if (item == config_.pinned.end()) {
            log::warning("launch request for unknown item \"" + launch->itemId + "\"");
            if (selfTest_) {
                ++selfTest_->unknownLaunchRequests;
            }
            return;
        }
        std::wstring error;
        const bool launched = launchItem(*item, error);
        if (!launched) {
            log::error("cannot launch \"" + item->id + "\": " + toUtf8(error));
            notify(L"Could not open " + toWide(item->name), error);
        }
        if (selfTest_) {
            if (launched) {
                selfTest_->launchedIds.push_back(item->id);
            } else {
                ++selfTest_->failedLaunches;
            }
        }
    }
}

void App::onWebViewProcessFailed()
{
    // The WebView2 browser process is gone, so every dock must be recreated.
    // Give up after repeated failures instead of looping.
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(browserFailures_, [&](auto time) { return now - time > std::chrono::minutes(5); });
    browserFailures_.push_back(now);

    docks_.clear();
    webViewEnvironment_.reset();
    if (browserFailures_.size() >= 3) {
        log::error("WebView2 keeps failing; dock disabled until MDU restarts");
        notify(L"Dock stopped", L"The dock's renderer failed repeatedly. Restart MDU to try again.");
        return;
    }
    webViewEnvironment_.create(paths_.webViewDataDirectory);
    rebuildDocks();
}

// --- Wallpaper ----------------------------------------------------------------

void App::applyWallpaperSetting()
{
    if (safeMode_ || !wallpaper_) {
        return;
    }

    std::string error;
    if (!config_.wallpaper.enabled) {
        if (!wallpaper_->restore(error)) {
            log::error("cannot restore previous wallpaper: " + error);
        }
        return;
    }

    wallpaper::Request request;
    if (!config_.wallpaper.image.empty()) {
        std::filesystem::path image = toWide(config_.wallpaper.image);
        if (image.is_relative() && theme_) {
            image = theme_->directory / image;
        }
        request.image = image;
    } else if (theme_) {
        request.image = theme_->wallpaper.image;
    }
    if (theme_) {
        request.background = theme_->wallpaper.background;
        request.feather = theme_->wallpaper.feather;
    }

    if (request.image.empty()) {
        notify(L"No wallpaper configured", L"The current theme has no wallpaper. Set wallpaper.image in the settings file.");
        return;
    }
    const bool applied = wallpaper_->apply(request, error);
    if (!applied) {
        log::error("wallpaper: " + error);
        notify(L"Wallpaper not applied", toWide(error));
    }
    if (selfTest_) {
        selfTest_->wallpaperAttempted = true;
        selfTest_->wallpaperApplied = applied;
        selfTest_->wallpaperError = error;
    }
}

// --- Tray -------------------------------------------------------------------

namespace {

enum TrayCommand : UINT {
    commandToggleDock = 100,
    commandToggleWallpaper,
    commandToggleStartup,
    commandEditSettings,
    commandReload,
    commandOpenDataFolder,
    commandRestartNormally,
    commandExit,
};

}  // namespace

void App::showTrayMenu(POINT anchor)
{
    const HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    const auto checked = [](bool value) { return static_cast<UINT>(MF_STRING | (value ? MF_CHECKED : MF_UNCHECKED)); };
    if (safeMode_) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Safe mode: dock and wallpaper are off");
        AppendMenuW(menu, MF_STRING, commandRestartNormally, L"Restart in normal mode");
    } else {
        AppendMenuW(menu, checked(config_.dock.enabled), commandToggleDock, L"Show dock");
        AppendMenuW(menu, checked(config_.wallpaper.enabled), commandToggleWallpaper, L"Use theme wallpaper");
    }
    AppendMenuW(menu, checked(config_.startWithWindows), commandToggleStartup, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, commandEditSettings, L"Edit settings file");
    AppendMenuW(menu, MF_STRING, commandReload, L"Reload settings");
    AppendMenuW(menu, MF_STRING, commandOpenDataFolder, L"Open data folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, commandExit, L"Exit MDU");

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(window_);
    const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const auto command = static_cast<UINT>(TrackPopupMenuEx(
        menu, alignment | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, anchor.x, anchor.y, window_, nullptr));
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command != 0) {
        handleTrayCommand(command);
    }
}

void App::handleTrayCommand(UINT command)
{
    switch (command) {
    case commandToggleDock:
        config_.dock.enabled = !config_.dock.enabled;
        saveConfiguration();
        rebuildDocks();
        break;

    case commandToggleWallpaper:
        config_.wallpaper.enabled = !config_.wallpaper.enabled;
        saveConfiguration();
        applyWallpaperSetting();
        break;

    case commandToggleStartup:
        config_.startWithWindows = !config_.startWithWindows;
        saveConfiguration();
        applyStartupRegistration();
        break;

    case commandEditSettings: {
        const auto file = paths_.configFile.wstring();
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) {
            const auto quoted = L"\"" + file + L"\"";
            ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    }

    case commandReload:
        reloadEverything();
        break;

    case commandOpenDataFolder:
        ShellExecuteW(nullptr, L"open", paths_.dataDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;

    case commandRestartNormally:
        crashGuard_.reset();
        restartRequested_ = true;
        DestroyWindow(window_);
        break;

    case commandExit:
        docks_.clear();
        DestroyWindow(window_);
        break;

    default:
        break;
    }
}

void App::notify(const std::wstring& title, const std::wstring& text)
{
    log::info(toUtf8(title) + ": " + toUtf8(text));
    if (tray_) {
        tray_->notify(title, text);
    }
}

}  // namespace mdu
