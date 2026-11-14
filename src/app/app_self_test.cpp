// Self-test mode (--self-test <report.json>).
//
// Starts MDU normally, then checks it from the outside the way a user would
// experience it: docks exist, are visible, are drawn and transparent, reserve
// their screen space, launch an item when clicked in the UI, reject malformed
// or malicious messages, refuse to navigate away, and apply and restore the
// wallpaper. Results, screenshots and the log are written next to the report so
// a CI machine can show what MDU actually looks like on Windows.

#include "app/app.h"

#include "dock/dock_layout.h"
#include "utilities/file_io.h"
#include "utilities/log.h"
#include "utilities/strings.h"
#include "windows/image_io.h"
#include "windows/monitors.h"
#include "windows/screen_capture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>

namespace mdu {

namespace {

constexpr UINT timeoutMs = 60'000;
constexpr UINT settleMs = 1'500;  // let the page render and the fade-in finish
constexpr UINT probeMs = 3'000;   // time for clicks, messages and the navigation attempt

std::string rectText(const Rect& r)
{
    return std::format("({}, {}) - ({}, {})", r.left, r.top, r.right, r.bottom);
}

nlohmann::json rectJson(const Rect& r)
{
    return {{"left", r.left}, {"top", r.top}, {"right", r.right}, {"bottom", r.bottom}};
}

Rect windowRect(HWND window)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    return toRect(rect);
}

Rect liveWorkArea(HMONITOR monitor)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return toRect(info.rcWork);
}

int luminanceAt(const wallpaper::Image& image, const Rect& origin, int x, int y)
{
    x -= origin.left;
    y -= origin.top;
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return -1;
    }
    const auto* p = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return (2126 * p[2] + 7152 * p[1] + 722 * p[0]) / 10000;
}

bool isInside(const std::filesystem::path& file, const std::filesystem::path& directory)
{
    const auto f = file.lexically_normal().wstring();
    const auto d = directory.lexically_normal().wstring();
    return f.size() > d.size() && _wcsnicmp(f.c_str(), d.c_str(), d.size()) == 0;
}

}  // namespace

void App::startSelfTest()
{
    log::info("self-test: started");
    SetTimer(window_, selfTestTimeoutTimer, timeoutMs, nullptr);

    const auto notepad = std::ranges::find_if(config_.pinned, [](const auto& item) { return item.id == "notepad"; });
    if (notepad != config_.pinned.end()) {
        selfTest_->probeItemId = notepad->id;
    } else if (!config_.pinned.empty()) {
        selfTest_->probeItemId = config_.pinned.front().id;
    }

    std::size_t expected = 0;
    if (config_.dock.enabled) {
        for (const auto& monitor : enumerateMonitors()) {
            expected += (config_.dock.monitors == DockMonitors::All || monitor.primary) ? 1 : 0;
        }
    }
    selfTest_->checks.push_back({"docks_created", expected > 0 && docks_.size() == expected,
                                 std::format("{} of {} expected", docks_.size(), expected)});
    if (docks_.empty()) {
        finishSelfTest(false);
    }
}

void App::selfTestOnDockReady()
{
    if (selfTest_->stage != SelfTestState::Stage::WaitingForDocks) {
        return;
    }
    if (std::ranges::all_of(docks_, [](const auto& dock) { return dock->pageReady(); })) {
        log::info("self-test: all docks ready");
        selfTest_->stage = SelfTestState::Stage::Settling;
        SetTimer(window_, selfTestStepTimer, settleMs, nullptr);
    }
}

void App::selfTestStep()
{
    switch (selfTest_->stage) {
    case SelfTestState::Stage::Settling:
        selfTestCheckDocks();
        selfTestScreenshot("screen");
        selfTestProbe();
        selfTest_->stage = SelfTestState::Stage::Probing;
        SetTimer(window_, selfTestStepTimer, probeMs, nullptr);
        break;

    case SelfTestState::Stage::Probing: {
        auto& checks = selfTest_->checks;
        auto& dock = *docks_.front();
        const auto& probe = selfTest_->probeItemId;
        const bool launchedProbe = std::ranges::find(selfTest_->launchedIds, probe) != selfTest_->launchedIds.end();

        checks.push_back({"launch_from_ui", !probe.empty() && launchedProbe && selfTest_->failedLaunches == 0,
                          std::format("clicked \"{}\"; launched {} item(s), {} failure(s)", probe,
                                      selfTest_->launchedIds.size(), selfTest_->failedLaunches)});
        checks.push_back({"invalid_messages_rejected",
                          dock.rejectedMessages() >= 3 && selfTest_->unknownLaunchRequests == 1 &&
                              selfTest_->launchedIds.size() == 1,
                          std::format("{} malformed rejected, {} unknown id ignored, {} launched in total",
                                      dock.rejectedMessages(), selfTest_->unknownLaunchRequests,
                                      selfTest_->launchedIds.size())});
        const auto uri = dock.currentUri();
        checks.push_back({"navigation_blocked",
                          dock.blockedNavigations() >= 1 && uri.starts_with(DockWindow::appOrigin),
                          std::format("{} blocked; page is {}", dock.blockedNavigations(), toUtf8(uri))});

        selfTestCheckWallpaper();
        finishSelfTest(false);
        break;
    }

    case SelfTestState::Stage::WaitingForDocks:
    case SelfTestState::Stage::Done:
        break;
    }
}

void App::selfTestCheckDocks()
{
    auto& checks = selfTest_->checks;

    checks.push_back({"webview2_runtime", !webViewEnvironment_.failed(),
                      webViewEnvironment_.failed()
                          ? std::format("error 0x{:08X}", static_cast<unsigned>(webViewEnvironment_.lastError()))
                          : "available"});
    checks.push_back({"theme_loaded", theme_.has_value(),
                      theme_ ? std::format("{} ({} variables)", theme_->id, theme_->variables.size()) : "none"});

    const auto withIcons = std::ranges::count_if(iconCache_, [](const auto& entry) { return !entry.second.empty(); });
    checks.push_back({"shell_icons", withIcons > 0, std::format("{} of {} items", withIcons, config_.pinned.size())});

    for (const auto& dock : docks_) {
        const auto& monitor = dock->monitor();
        const auto name = toUtf8(monitor.deviceName);
        const Rect rect = windowRect(dock->window());
        const bool inside = rect.width() > 0 && rect.height() > 0 && rect.left >= monitor.bounds.left &&
                            rect.top >= monitor.bounds.top && rect.right <= monitor.bounds.right &&
                            rect.bottom <= monitor.bounds.bottom;

        checks.push_back({"dock_visible " + name, IsWindowVisible(dock->window()) != FALSE, rectText(rect)});
        checks.push_back({"dock_on_its_monitor " + name, inside,
                          rectText(rect) + " within " + rectText(monitor.bounds)});

        if (config_.dock.placement == DockPlacement::Reserve) {
            const Rect work = liveWorkArea(monitor.handle);
            bool clear = false;
            switch (config_.dock.edge) {
            case DockEdge::Bottom: clear = work.bottom <= rect.top + 1; break;
            case DockEdge::Left: clear = work.left >= rect.right - 1; break;
            case DockEdge::Right: clear = work.right <= rect.left + 1; break;
            }
            checks.push_back({"work_area_reserved " + name, clear,
                              "work area " + rectText(work) + ", dock " + rectText(rect)});
        }
    }
}

void App::selfTestScreenshot(const std::string& name)
{
    const auto directory = options_.selfTestReport.parent_path();
    const Rect screen = virtualScreenBounds();
    const auto capture = captureScreen(screen);
    std::string error;
    const bool saved = capture && savePng(imagingFactory_.Get(), *capture, directory / (name + ".png"), error);
    selfTest_->checks.push_back({"screenshot", saved, saved ? name + ".png " + rectText(screen) : error});
    if (!capture) {
        return;
    }

    int index = 0;
    for (const auto& dock : docks_) {
        const Rect rect = windowRect(dock->window());
        const unsigned dpi = dock->monitor().dpi;
        const auto label = toUtf8(dock->monitor().deviceName);

        const Rect around{rect.left - 24, rect.top - 24, rect.right + 24, rect.bottom + 24};
        savePng(imagingFactory_.Get(), cropImage(*capture, screen, around),
                directory / std::format("dock-{}.png", ++index), error);

        // Inside the bar: the dark themed surface must be drawn.
        // Inside the transparent margin: whatever is behind must show through.
        const int inset = dipsToPixels(dockMetricsFor(config_.dock.iconSize).shadowInset, dpi);
        const bool horizontal = config_.dock.edge == DockEdge::Bottom;
        const int barX = horizontal ? rect.left + inset + dipsToPixels(5, dpi) : (rect.left + rect.right) / 2;
        const int barY = horizontal ? (rect.top + rect.bottom) / 2 : rect.top + inset + dipsToPixels(5, dpi);
        const int bar = luminanceAt(*capture, screen, barX, barY);
        const int margin = luminanceAt(*capture, screen, rect.left + 1, rect.top + 1);
        const int outside = luminanceAt(*capture, screen, rect.left - 4, rect.top - 4);

        selfTest_->checks.push_back({"dock_drawn " + label, bar >= 0 && bar < 90,
                                     std::format("bar luminance {} (expected dark)", bar)});
        // Transparency is judged against the pixel just outside the window,
        // which shows the same desktop.
        selfTest_->checks.push_back({"dock_transparent " + label,
                                     margin >= 0 && outside >= 0 && std::abs(margin - outside) <= 24,
                                     std::format("margin luminance {}, desktop beside it {}", margin, outside)});
    }
}

void App::selfTestProbe()
{
    if (docks_.empty()) {
        return;
    }
    // The probe id comes from configuration and is validated as [a-z0-9_-],
    // so it is safe to embed in the script.
    const auto script = std::format(LR"((() => {{
  const item = document.querySelector('.item[data-id="{}"]');
  if (item) item.click();
  const post = (m) => window.chrome.webview.postMessage(m);
  post(JSON.stringify({{ type: 'launch', id: '../../Windows/System32/calc' }}));
  post(JSON.stringify({{ type: 'exec', command: 'calc.exe' }}));
  post('not json');
  post(JSON.stringify({{ type: 'launch', id: 'not-configured' }}));
  setTimeout(() => {{ location.href = 'https://example.com/'; }}, 300);
}})();)",
                                    toWide(selfTest_->probeItemId));
    docks_.front()->executeScript(script);
}

void App::selfTestCheckWallpaper()
{
    if (!config_.wallpaper.enabled || !wallpaper_) {
        return;
    }
    auto& checks = selfTest_->checks;
    const auto current = std::filesystem::path(wallpaper::Manager::currentWallpaper());
    checks.push_back({"wallpaper_applied",
                      selfTest_->wallpaperApplied && isInside(current, paths_.cacheDirectory) &&
                          wallpaper_->hasRestoreState(),
                      selfTest_->wallpaperAttempted ? (selfTest_->wallpaperApplied ? toUtf8(current.wstring())
                                                                                   : selfTest_->wallpaperError)
                                                    : "not attempted"});

    std::string error;
    const bool restored = wallpaper_->restore(error);
    const auto after = std::filesystem::path(wallpaper::Manager::currentWallpaper());
    checks.push_back({"wallpaper_restored",
                      restored && !wallpaper_->hasRestoreState() && !isInside(after, paths_.cacheDirectory),
                      restored ? "now " + toUtf8(after.wstring()) : error});
}

void App::finishSelfTest(bool timedOut)
{
    if (!selfTest_ || selfTest_->stage == SelfTestState::Stage::Done) {
        return;
    }
    const auto stageAtEnd = selfTest_->stage;
    selfTest_->stage = SelfTestState::Stage::Done;
    KillTimer(window_, selfTestTimeoutTimer);
    KillTimer(window_, selfTestStepTimer);

    if (timedOut) {
        selfTest_->checks.push_back({"completed_in_time", false,
                                     std::format("timed out at stage {}", static_cast<int>(stageAtEnd))});
    }
    // Never leave a test wallpaper behind.
    if (wallpaper_ && wallpaper_->hasRestoreState()) {
        std::string ignored;
        wallpaper_->restore(ignored);
    }

    nlohmann::json monitors = nlohmann::json::array();
    for (const auto& monitor : enumerateMonitors()) {
        monitors.push_back({{"device", toUtf8(monitor.deviceName)},
                            {"bounds", rectJson(monitor.bounds)},
                            {"workArea", rectJson(liveWorkArea(monitor.handle))},
                            {"dpi", monitor.dpi},
                            {"primary", monitor.primary}});
    }
    nlohmann::json docks = nlohmann::json::array();
    for (const auto& dock : docks_) {
        docks.push_back({{"device", toUtf8(dock->monitor().deviceName)},
                         {"window", rectJson(windowRect(dock->window()))},
                         {"pageReady", dock->pageReady()},
                         {"visible", IsWindowVisible(dock->window()) != FALSE}});
    }
    nlohmann::json checks = nlohmann::json::array();
    bool passed = !selfTest_->checks.empty();
    for (const auto& check : selfTest_->checks) {
        passed = passed && check.passed;
        checks.push_back({{"name", check.name}, {"passed", check.passed}, {"detail", check.detail}});
        log::info(std::format("self-test: [{}] {} - {}", check.passed ? "pass" : "FAIL", check.name, check.detail));
    }

    const nlohmann::json report = {
        {"version", MDU_VERSION}, {"passed", passed},   {"timedOut", timedOut},
        {"monitors", monitors},   {"docks", docks},     {"checks", checks},
        {"log", toUtf8(paths_.logFile.wstring())},
    };
    std::string error;
    if (!writeFileAtomically(options_.selfTestReport,
                             report.dump(2, ' ', false, nlohmann::json::error_handler_t::replace), error)) {
        log::error("self-test: cannot write report: " + error);
    }

    selfTest_->exitCode = timedOut ? 2 : (passed ? 0 : 3);
    log::info(std::format("self-test: finished, exit code {}", selfTest_->exitCode));
    docks_.clear();
    if (window_) {
        DestroyWindow(window_);
    }
}

}  // namespace mdu
