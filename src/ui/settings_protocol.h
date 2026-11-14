#pragma once

#include "settings/config.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Messages between the settings page and the native side.
//
// The page can change preferences, reorder, rename and remove pinned items,
// and ask the native side to open a file picker. It can never supply a path,
// a command or an item target: those only come from native file dialogs.
// Messages that change settings are validated against the current
// configuration and produce the complete new configuration.
namespace mdu::settings_protocol {

struct Ready {};
struct ConfigChange {
    Config config;
};
struct AddItem {};           // native "choose an app or file" dialog
struct ChooseWallpaper {};   // native "choose an image" dialog
struct OpenSettingsFile {};  // open config.json in the user's editor

using Message = std::variant<Ready, ConfigChange, AddItem, ChooseWallpaper, OpenSettingsFile>;

constexpr std::size_t maxMessageSize = 16 * 1024;

std::optional<Message> parse(std::string_view json, const Config& current, const std::vector<std::string>& themeIds);

struct ThemeOption {
    std::string id;
    std::string name;
};

struct StateView {
    std::string version;
    std::vector<ThemeOption> themes;
    std::map<std::string, std::string> icons;  // item id -> data: URL
    std::string wallpaperImageName;  // file name only, for display; empty = theme image
    std::string notice;              // short message to show, e.g. after a failed save
    std::vector<std::pair<std::string, std::string>> themeVariables;  // validated CSS custom properties
    std::string themeStylesheet;  // URL on the trusted theme host, or empty
};

std::string buildState(const Config& config, const StateView& view);

}  // namespace mdu::settings_protocol
