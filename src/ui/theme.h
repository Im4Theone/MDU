#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mdu {

struct ThemeColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    constexpr bool operator==(const ThemeColor&) const = default;
};

struct ThemeWallpaper {
    std::filesystem::path image;  // absolute after loading; empty if the theme has none
    std::optional<ThemeColor> background;  // letterbox colour; sampled from the image if absent
    double feather = 0.04;  // fraction of the shorter image side blended into the background
};

// A theme is a directory containing theme.json and optional assets. The core
// application only knows the generic fields below; everything visual is
// expressed as CSS custom properties and an optional stylesheet.
struct Theme {
    std::string id;    // directory name
    std::string name;  // display name
    std::filesystem::path directory;
    std::vector<std::pair<std::string, std::string>> variables;  // CSS custom properties without "--"
    std::string stylesheet;  // file name relative to the theme directory, empty if none
    ThemeWallpaper wallpaper;
};

struct ThemeLoadResult {
    std::optional<Theme> theme;
    std::vector<std::string> warnings;
};

ThemeLoadResult parseTheme(std::string_view json, const std::filesystem::path& directory);
ThemeLoadResult loadTheme(const std::filesystem::path& themesRoot, std::string_view id);

// Validation used for anything that ends up in CSS. Values are limited to a
// conservative character set so a theme cannot inject rules or load remote
// resources.
bool isValidCssVariableName(std::string_view name);
bool isValidCssVariableValue(std::string_view value);

// A relative path that stays inside its base directory: no root, no drive, no "..".
bool isSafeRelativePath(std::string_view path);

std::optional<ThemeColor> parseHexColor(std::string_view text);

}  // namespace mdu
