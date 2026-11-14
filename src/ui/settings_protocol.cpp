#include "ui/settings_protocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace mdu::settings_protocol {

namespace {

using nlohmann::json;

bool onlyKeys(const json& object, std::initializer_list<std::string_view> allowed)
{
    return std::ranges::all_of(object.items(), [&](const auto& entry) {
        return std::ranges::find(allowed, std::string_view(entry.key())) != allowed.end();
    });
}

// Copies `key` from `patch` into `target` if present and of the same JSON type.
bool copyTyped(const json& patch, json& target, const char* key)
{
    const auto it = patch.find(key);
    if (it == patch.end()) {
        return true;
    }
    const auto& current = target[key];
    const bool sameType = (current.is_boolean() && it->is_boolean()) ||
                          (current.is_string() && it->is_string()) ||
                          (current.is_number_integer() && it->is_number_integer());
    if (!sameType) {
        return false;
    }
    target[key] = *it;
    return true;
}

std::optional<Config> applyUpdate(const json& patch, const Config& current, const std::vector<std::string>& themeIds)
{
    if (!patch.is_object() || !onlyKeys(patch, {"startWithWindows", "theme", "motion", "dock", "wallpaper"})) {
        return std::nullopt;
    }

    json merged = json::parse(serializeConfig(current));
    if (!copyTyped(patch, merged, "startWithWindows") || !copyTyped(patch, merged, "theme") ||
        !copyTyped(patch, merged, "motion")) {
        return std::nullopt;
    }

    if (const auto dock = patch.find("dock"); dock != patch.end()) {
        if (!dock->is_object() ||
            !onlyKeys(*dock, {"enabled", "edge", "monitors", "placement", "iconSize", "margin"})) {
            return std::nullopt;
        }
        for (const char* key : {"enabled", "edge", "monitors", "placement", "iconSize", "margin"}) {
            if (!copyTyped(*dock, merged["dock"], key)) {
                return std::nullopt;
            }
        }
    }
    if (const auto wallpaper = patch.find("wallpaper"); wallpaper != patch.end()) {
        // Only the switch: the image path is chosen through a native dialog.
        if (!wallpaper->is_object() || !onlyKeys(*wallpaper, {"enabled"}) ||
            !copyTyped(*wallpaper, merged["wallpaper"], "enabled")) {
            return std::nullopt;
        }
    }

    // Reuse the configuration parser for range and enum validation; any
    // warning means a value was rejected.
    auto result = parseConfig(merged.dump());
    if (!result.warnings.empty()) {
        return std::nullopt;
    }
    if (std::ranges::find(themeIds, result.config.theme) == themeIds.end()) {
        return std::nullopt;
    }
    return std::move(result.config);
}

std::optional<Config> applyOrder(const json& order, const Config& current)
{
    if (!order.is_array()) {
        return std::nullopt;
    }
    Config next = current;
    next.pinned.clear();
    std::set<std::string> seen;
    for (const auto& value : order) {
        if (!value.is_string()) {
            return std::nullopt;
        }
        const auto& id = value.get_ref<const std::string&>();
        const auto item = std::ranges::find_if(current.pinned, [&](const auto& p) { return p.id == id; });
        if (item == current.pinned.end() || !seen.insert(id).second) {
            return std::nullopt;
        }
        next.pinned.push_back(*item);
    }
    return next;
}

bool isValidDisplayName(std::string_view name)
{
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return std::ranges::none_of(name, [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

std::optional<Config> applyRename(const json& root, const Config& current)
{
    const auto id = root.find("id");
    const auto name = root.find("name");
    if (id == root.end() || name == root.end() || !id->is_string() || !name->is_string() ||
        !isValidDisplayName(name->get_ref<const std::string&>())) {
        return std::nullopt;
    }
    Config next = current;
    const auto item = std::ranges::find_if(next.pinned, [&](const auto& p) { return p.id == id->get<std::string>(); });
    if (item == next.pinned.end()) {
        return std::nullopt;
    }
    item->name = name->get<std::string>();
    return next;
}

}  // namespace

std::optional<Message> parse(std::string_view text, const Config& current, const std::vector<std::string>& themeIds)
{
    if (text.empty() || text.size() > maxMessageSize) {
        return std::nullopt;
    }
    const auto root = json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto type = root.find("type");
    if (type == root.end() || !type->is_string()) {
        return std::nullopt;
    }
    const auto& name = type->get_ref<const std::string&>();

    if (name == "settings-ready") {
        return Ready{};
    }
    if (name == "settings-add-item") {
        return AddItem{};
    }
    if (name == "settings-choose-wallpaper") {
        return ChooseWallpaper{};
    }
    if (name == "settings-open-file") {
        return OpenSettingsFile{};
    }

    std::optional<Config> next;
    if (name == "settings-update") {
        if (const auto settings = root.find("settings"); settings != root.end()) {
            next = applyUpdate(*settings, current, themeIds);
        }
    } else if (name == "settings-items") {
        if (const auto order = root.find("order"); order != root.end()) {
            next = applyOrder(*order, current);
        }
    } else if (name == "settings-rename") {
        next = applyRename(root, current);
    } else if (name == "settings-reset-wallpaper") {
        next = current;
        next->wallpaper.image.clear();
    }

    if (!next) {
        return std::nullopt;
    }
    return ConfigChange{std::move(*next)};
}

std::string buildState(const Config& config, const StateView& view)
{
    // The settings object uses exactly the configuration file's format.
    json settings = json::parse(serializeConfig(config));
    settings.erase("pinned");
    settings["wallpaper"].erase("image");

    json items = json::array();
    for (const auto& item : config.pinned) {
        const auto icon = view.icons.find(item.id);
        items.push_back({{"id", item.id},
                         {"name", item.name},
                         {"target", item.target},
                         {"icon", icon != view.icons.end() ? icon->second : std::string()}});
    }
    json themes = json::array();
    for (const auto& theme : view.themes) {
        themes.push_back({{"id", theme.id}, {"name", theme.name}});
    }
    json variables = json::object();
    for (const auto& [name, value] : view.themeVariables) {
        variables[name] = value;
    }

    const json message = {
        {"type", "settings-state"},
        {"version", view.version},
        {"settings", settings},
        {"items", items},
        {"themes", themes},
        {"wallpaperImage", view.wallpaperImageName},
        {"notice", view.notice},
        {"theme", {{"variables", variables}, {"stylesheet", view.themeStylesheet}}},
    };
    return message.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace mdu::settings_protocol
