#include "settings/config.h"

#include "utilities/file_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <utility>

namespace mdu {

namespace {

using nlohmann::json;

template <typename Enum>
struct EnumName {
    Enum value;
    std::string_view name;
};

constexpr std::array dockEdgeNames{
    EnumName<DockEdge>{DockEdge::Bottom, "bottom"},
    EnumName<DockEdge>{DockEdge::Left, "left"},
    EnumName<DockEdge>{DockEdge::Right, "right"},
};

constexpr std::array dockMonitorNames{
    EnumName<DockMonitors>{DockMonitors::All, "all"},
    EnumName<DockMonitors>{DockMonitors::Primary, "primary"},
};

constexpr std::array dockPlacementNames{
    EnumName<DockPlacement>{DockPlacement::Reserve, "reserve"},
    EnumName<DockPlacement>{DockPlacement::Overlay, "overlay"},
};

constexpr std::array motionNames{
    EnumName<MotionPreference>{MotionPreference::System, "system"},
    EnumName<MotionPreference>{MotionPreference::Reduced, "reduced"},
    EnumName<MotionPreference>{MotionPreference::Full, "full"},
};

template <typename Enum, std::size_t N>
std::string_view enumToString(const std::array<EnumName<Enum>, N>& names, Enum value)
{
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return names.front().name;
}

// Reads typed fields from a JSON object and records a warning for every value
// that is present but unusable.
class FieldReader {
public:
    FieldReader(const json& object, std::string prefix, std::vector<std::string>& warnings)
        : object_(object), prefix_(std::move(prefix)), warnings_(warnings)
    {
    }

    void readBool(const char* key, bool& out) const
    {
        if (const auto* value = find(key)) {
            if (value->is_boolean()) {
                out = value->get<bool>();
            } else {
                warn(key, "expected true or false");
            }
        }
    }

    void readString(const char* key, std::string& out) const
    {
        if (const auto* value = find(key)) {
            if (value->is_string()) {
                out = value->get<std::string>();
            } else {
                warn(key, "expected a string");
            }
        }
    }

    void readInt(const char* key, int& out, int minimum, int maximum) const
    {
        if (const auto* value = find(key)) {
            if (value->is_number_integer()) {
                const auto number = value->get<long long>();
                if (number >= minimum && number <= maximum) {
                    out = static_cast<int>(number);
                    return;
                }
            }
            warn(key, "expected an integer between " + std::to_string(minimum) + " and " +
                          std::to_string(maximum));
        }
    }

    template <typename Enum, std::size_t N>
    void readEnum(const char* key, Enum& out, const std::array<EnumName<Enum>, N>& names) const
    {
        const auto* value = find(key);
        if (!value) {
            return;
        }
        if (value->is_string()) {
            const auto text = value->get<std::string>();
            for (const auto& entry : names) {
                if (entry.name == text) {
                    out = entry.value;
                    return;
                }
            }
        }
        std::string allowed;
        for (const auto& entry : names) {
            allowed += allowed.empty() ? "" : ", ";
            allowed += entry.name;
        }
        warn(key, "expected one of: " + allowed);
    }

    const json* find(const char* key) const
    {
        const auto it = object_.find(key);
        return it == object_.end() ? nullptr : &*it;
    }

    void warn(const char* key, const std::string& message) const
    {
        warnings_.push_back(prefix_ + key + ": " + message + "; using default");
    }

private:
    const json& object_;
    std::string prefix_;
    std::vector<std::string>& warnings_;
};

void readDock(const json& object, DockSettings& dock, std::vector<std::string>& warnings)
{
    const FieldReader reader(object, "dock.", warnings);
    reader.readBool("enabled", dock.enabled);
    reader.readEnum("edge", dock.edge, dockEdgeNames);
    reader.readEnum("monitors", dock.monitors, dockMonitorNames);
    reader.readEnum("placement", dock.placement, dockPlacementNames);
    reader.readInt("iconSize", dock.iconSize, 24, 128);
    reader.readInt("margin", dock.margin, 0, 64);
}

std::optional<PinnedItem> readPinnedItem(const json& value, std::size_t index,
                                         std::vector<std::string>& warnings)
{
    const auto prefix = "pinned[" + std::to_string(index) + "].";
    if (!value.is_object()) {
        warnings.push_back("pinned[" + std::to_string(index) + "]: expected an object; skipped");
        return std::nullopt;
    }

    PinnedItem item;
    const FieldReader reader(value, prefix, warnings);
    reader.readString("id", item.id);
    reader.readString("name", item.name);
    reader.readString("target", item.target);
    reader.readString("arguments", item.arguments);

    if (!isValidItemId(item.id)) {
        warnings.push_back(prefix + "id: must be 1-64 characters of a-z, 0-9, '-' or '_'; skipped");
        return std::nullopt;
    }
    if (item.target.empty()) {
        warnings.push_back(prefix + "target: must not be empty; skipped");
        return std::nullopt;
    }
    if (item.name.empty()) {
        item.name = item.id;
    }
    return item;
}

}  // namespace

bool isValidItemId(std::string_view id)
{
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::ranges::all_of(id, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string makeUniqueItemId(std::string_view name, const std::vector<PinnedItem>& existing)
{
    std::string base;
    for (const char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            base += c;
        } else if (c >= 'A' && c <= 'Z') {
            base += static_cast<char>(c - 'A' + 'a');
        } else if (!base.empty() && base.back() != '-') {
            base += '-';
        }
    }
    while (!base.empty() && base.back() == '-') {
        base.pop_back();
    }
    if (base.empty()) {
        base = "item";
    }
    base.resize(std::min<std::size_t>(base.size(), 56));

    const auto taken = [&](const std::string& id) {
        return std::ranges::any_of(existing, [&](const PinnedItem& item) { return item.id == id; });
    };
    std::string candidate = base;
    for (int suffix = 2; taken(candidate); ++suffix) {
        candidate = base + "-" + std::to_string(suffix);
    }
    return candidate;
}

Config defaultConfig()
{
    Config config;
    config.pinned = {
        {"explorer", "File Explorer", "%WINDIR%\\explorer.exe", ""},
        {"settings", "Settings", "ms-settings:", ""},
        {"notepad", "Notepad", "%WINDIR%\\System32\\notepad.exe", ""},
    };
    return config;
}

ConfigLoadResult parseConfig(std::string_view text)
{
    ConfigLoadResult result{defaultConfig(), {}, false};

    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        result.fileWasInvalid = true;
        result.warnings.emplace_back("configuration is not a valid JSON object; using defaults");
        return result;
    }

    auto& config = result.config;
    const FieldReader reader(root, "", result.warnings);
    reader.readBool("startWithWindows", config.startWithWindows);
    reader.readString("theme", config.theme);
    reader.readEnum("motion", config.motion, motionNames);

    if (config.theme.empty() || !isValidItemId(config.theme)) {
        result.warnings.emplace_back("theme: invalid theme name; using \"miku\"");
        config.theme = "miku";
    }

    if (const auto* dock = reader.find("dock")) {
        if (dock->is_object()) {
            readDock(*dock, config.dock, result.warnings);
        } else {
            reader.warn("dock", "expected an object");
        }
    }

    if (const auto* wallpaper = reader.find("wallpaper")) {
        if (wallpaper->is_object()) {
            const FieldReader wallpaperReader(*wallpaper, "wallpaper.", result.warnings);
            wallpaperReader.readBool("enabled", config.wallpaper.enabled);
            wallpaperReader.readString("image", config.wallpaper.image);
        } else {
            reader.warn("wallpaper", "expected an object");
        }
    }

    if (const auto* pinned = reader.find("pinned")) {
        if (pinned->is_array()) {
            config.pinned.clear();
            std::set<std::string> seen;
            for (std::size_t i = 0; i < pinned->size(); ++i) {
                auto item = readPinnedItem((*pinned)[i], i, result.warnings);
                if (!item) {
                    continue;
                }
                if (!seen.insert(item->id).second) {
                    result.warnings.push_back("pinned[" + std::to_string(i) + "].id: duplicate \"" +
                                              item->id + "\"; skipped");
                    continue;
                }
                config.pinned.push_back(std::move(*item));
            }
        } else {
            reader.warn("pinned", "expected an array");
        }
    }

    return result;
}

std::string serializeConfig(const Config& config)
{
    json pinned = json::array();
    for (const auto& item : config.pinned) {
        json entry = {{"id", item.id}, {"name", item.name}, {"target", item.target}};
        if (!item.arguments.empty()) {
            entry["arguments"] = item.arguments;
        }
        pinned.push_back(std::move(entry));
    }

    // nlohmann::ordered_json keeps keys in a human-friendly order.
    nlohmann::ordered_json root;
    root["startWithWindows"] = config.startWithWindows;
    root["theme"] = config.theme;
    root["motion"] = enumToString(motionNames, config.motion);
    root["dock"] = {
        {"enabled", config.dock.enabled},
        {"edge", enumToString(dockEdgeNames, config.dock.edge)},
        {"monitors", enumToString(dockMonitorNames, config.dock.monitors)},
        {"placement", enumToString(dockPlacementNames, config.dock.placement)},
        {"iconSize", config.dock.iconSize},
        {"margin", config.dock.margin},
    };
    root["wallpaper"] = {
        {"enabled", config.wallpaper.enabled},
        {"image", config.wallpaper.image},
    };
    root["pinned"] = pinned;
    return root.dump(2) + "\n";
}

ConfigLoadResult loadConfigFile(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {defaultConfig(), {}, false};
    }

    const auto text = readTextFile(path);
    if (!text) {
        ConfigLoadResult result{defaultConfig(), {"cannot read configuration file; using defaults"}, true};
        return result;
    }
    return parseConfig(*text);
}

bool saveConfigFile(const std::filesystem::path& path, const Config& config, std::string& error)
{
    return writeFileAtomically(path, serializeConfig(config), error);
}

}  // namespace mdu
