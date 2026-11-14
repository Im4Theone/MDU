// The settings window: opening it, interpreting its messages and applying
// configuration changes.

#include "app/app.h"

#include "utilities/log.h"
#include "utilities/strings.h"
#include "windows/file_picker.h"

#include <shellapi.h>

#include <algorithm>
#include <format>

namespace mdu {

void App::openSettings()
{
    if (safeMode_) {
        return;
    }
    if (settings_) {
        settings_->bringToFront();
        return;
    }

    settings_ = SettingsWindow::create(
        instance_, {paths_.webDirectory, paths_.themesDirectory},
        SettingsWindow::Callbacks{
            .message = [this](const std::string& text) { onSettingsMessage(text); },
            .closeRequested = [this] { PostMessageW(window_, settingsClosedMessage, 0, 0); },
            .browserFailed = [this](COREWEBVIEW2_PROCESS_FAILED_KIND) {
                PostMessageW(window_, webViewFailedMessage, 0, 0);
            },
        });
    if (!settings_) {
        notify(L"Settings unavailable", L"The settings window could not be opened. Details are in the log.");
        return;
    }

    webViewEnvironment_.create(paths_.webViewDataDirectory);  // no-op if it already exists
    webViewEnvironment_.whenReady([this](ICoreWebView2Environment* environment) {
        if (settings_ && environment) {
            settings_->attachWebView(environment);
        }
    });
    sendSettingsState();
}

std::vector<settings_protocol::ThemeOption> App::availableThemes() const
{
    std::vector<settings_protocol::ThemeOption> themes;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(paths_.themesDirectory, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const auto id = toUtf8(entry.path().filename().wstring());
        if (const auto loaded = loadTheme(paths_.themesDirectory, id); loaded.theme) {
            themes.push_back({loaded.theme->id, loaded.theme->name});
        }
    }
    std::ranges::sort(themes, {}, &settings_protocol::ThemeOption::name);
    return themes;
}

void App::sendSettingsState(const std::string& notice)
{
    if (!settings_) {
        return;
    }
    settings_protocol::StateView view;
    view.version = MDU_VERSION;
    view.themes = availableThemes();
    view.icons = iconCache_;
    view.notice = notice;
    if (!config_.wallpaper.image.empty()) {
        view.wallpaperImageName = toUtf8(std::filesystem::path(toWide(config_.wallpaper.image)).filename().wstring());
    }
    if (theme_) {
        view.themeVariables = theme_->variables;
        if (!theme_->stylesheet.empty()) {
            auto relative = theme_->stylesheet;
            std::ranges::replace(relative, '\\', '/');
            view.themeStylesheet = toUtf8(WebViewHost::themesOrigin) + theme_->id + "/" + relative;
        }
    }
    settings_->postState(settings_protocol::buildState(config_, view));
}

void App::onSettingsMessage(const std::string& text)
{
    std::vector<std::string> themeIds;
    for (const auto& theme : availableThemes()) {
        themeIds.push_back(theme.id);
    }

    const auto message = settings_protocol::parse(text, config_, themeIds);
    if (!message) {
        ++rejectedSettingsMessages_;
        log::warning("ignored invalid message from the settings page");
        sendSettingsState();  // put the page back in sync with the real settings
        return;
    }

    if (std::holds_alternative<settings_protocol::Ready>(*message)) {
        settings_->setPageReady();
        if (selfTestActive()) {
            selfTestOnSettingsReady();
        }
    } else if (const auto* change = std::get_if<settings_protocol::ConfigChange>(&*message)) {
        applyConfig(change->config);
    } else if (std::holds_alternative<settings_protocol::AddItem>(*message)) {
        if (const auto picked = pickAppOrFile(settings_->window())) {
            Config next = config_;
            const auto name = toUtf8(picked->displayName);
            next.pinned.push_back({makeUniqueItemId(name, next.pinned), name, toUtf8(picked->path), ""});
            applyConfig(std::move(next));
        }
    } else if (std::holds_alternative<settings_protocol::ChooseWallpaper>(*message)) {
        if (const auto picked = pickImage(settings_->window())) {
            Config next = config_;
            next.wallpaper.image = toUtf8(picked->path);
            next.wallpaper.enabled = true;
            applyConfig(std::move(next));
        }
    } else if (std::holds_alternative<settings_protocol::OpenSettingsFile>(*message)) {
        openSettingsFile();
    }
}

void App::applyConfig(Config next)
{
    const Config previous = std::move(config_);
    config_ = std::move(next);

    std::string error;
    const bool saved = saveConfigFile(paths_.configFile, config_, error);
    if (!saved) {
        log::error("cannot save configuration: " + error);
    }

    if (config_.startWithWindows != previous.startWithWindows) {
        applyStartupRegistration();
    }
    const bool themeChanged = config_.theme != previous.theme;
    if (themeChanged) {
        loadActiveTheme();
    }
    if (!safeMode_) {
        if (config_.pinned != previous.pinned || config_.dock.iconSize != previous.dock.iconSize) {
            refreshIcons();
        }
        rebuildDocks();  // re-lays out and re-sends state; existing docks are kept
        if (themeChanged || config_.wallpaper != previous.wallpaper) {
            applyWallpaperSetting();
        }
    }
    sendSettingsState(saved ? std::string() : "Your changes are active but could not be saved: " + error);
}

void App::openSettingsFile()
{
    std::error_code ec;
    if (!std::filesystem::exists(paths_.configFile, ec)) {
        saveConfiguration();
    }
    const auto file = paths_.configFile.wstring();
    const auto result =
        reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        // No program is associated with .json files.
        const auto quoted = L"\"" + file + L"\"";
        ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

}  // namespace mdu
