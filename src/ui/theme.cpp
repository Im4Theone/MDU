#include "ui/theme.h"

#include "settings/config.h"
#include "utilities/file_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace mdu {

namespace {

bool isLowerAlnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool containsIgnoringCase(std::string_view haystack, std::string_view needle)
{
    const auto it = std::ranges::search(haystack, needle, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return !it.empty();
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool isValidCssVariableName(std::string_view name)
{
    if (name.empty() || name.size() > 48 || !(name.front() >= 'a' && name.front() <= 'z')) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) { return isLowerAlnum(c) || c == '-'; });
}

bool isValidCssVariableValue(std::string_view value)
{
    if (value.empty() || value.size() > 200) {
        return false;
    }
    const bool allowedCharacters = std::ranges::all_of(value, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return std::isalnum(u) || std::string_view(" #%.,()-+/\"'_").find(c) != std::string_view::npos;
    });
    // url() and friends could reference resources; colours, sizes, durations,
    // easing functions and font stacks never need them.
    return allowedCharacters && !containsIgnoringCase(value, "url(") &&
           !containsIgnoringCase(value, "image-set(") && !containsIgnoringCase(value, "expression(");
}

bool isSafeRelativePath(std::string_view path)
{
    if (path.empty() || path.size() > 200 || path.front() == '/' || path.front() == '\\') {
        return false;
    }
    if (path.find(':') != std::string_view::npos) {
        return false;  // drive letters and alternate data streams
    }
    if (std::ranges::any_of(path, [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
        return false;
    }

    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find_first_of("/\\", start);
        const auto component = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (component == ".." || component.empty()) {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

std::optional<ThemeColor> parseHexColor(std::string_view text)
{
    if (text.size() != 7 || text.front() != '#') {
        return std::nullopt;
    }
    int values[6];
    for (std::size_t i = 0; i < 6; ++i) {
        values[i] = hexDigit(text[i + 1]);
        if (values[i] < 0) {
            return std::nullopt;
        }
    }
    return ThemeColor{static_cast<std::uint8_t>(values[0] * 16 + values[1]),
                      static_cast<std::uint8_t>(values[2] * 16 + values[3]),
                      static_cast<std::uint8_t>(values[4] * 16 + values[5])};
}

ThemeLoadResult parseTheme(std::string_view text, const std::filesystem::path& directory)
{
    ThemeLoadResult result;
    const auto root = nlohmann::json::parse(text, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        result.warnings.emplace_back("theme.json is not a valid JSON object");
        return result;
    }

    Theme theme;
    theme.directory = directory;
    theme.id = directory.filename().string();
    theme.name = theme.id;
    if (const auto it = root.find("name"); it != root.end() && it->is_string()) {
        theme.name = it->get<std::string>();
    }

    if (const auto it = root.find("variables"); it != root.end()) {
        if (it->is_object()) {
            for (const auto& [name, value] : it->items()) {
                if (!value.is_string()) {
                    result.warnings.push_back("variables." + name + ": expected a string; ignored");
                    continue;
                }
                const auto cssValue = value.get<std::string>();
                if (!isValidCssVariableName(name) || !isValidCssVariableValue(cssValue)) {
                    result.warnings.push_back("variables." + name + ": invalid name or value; ignored");
                    continue;
                }
                theme.variables.emplace_back(name, cssValue);
            }
        } else {
            result.warnings.emplace_back("variables: expected an object; ignored");
        }
    }

    if (const auto it = root.find("stylesheet"); it != root.end()) {
        if (it->is_string() && isSafeRelativePath(it->get<std::string>()) &&
            it->get<std::string>().ends_with(".css")) {
            theme.stylesheet = it->get<std::string>();
        } else {
            result.warnings.emplace_back("stylesheet: expected a relative .css path; ignored");
        }
    }

    if (const auto it = root.find("wallpaper"); it != root.end() && it->is_object()) {
        const auto& wallpaper = *it;
        if (const auto image = wallpaper.find("image"); image != wallpaper.end()) {
            if (image->is_string() && isSafeRelativePath(image->get<std::string>())) {
                theme.wallpaper.image = directory / std::filesystem::path(image->get<std::string>());
            } else {
                result.warnings.emplace_back("wallpaper.image: expected a relative path; ignored");
            }
        }
        if (const auto background = wallpaper.find("background"); background != wallpaper.end()) {
            const auto color = background->is_string() ? parseHexColor(background->get<std::string>())
                                                       : std::nullopt;
            if (color) {
                theme.wallpaper.background = color;
            } else {
                result.warnings.emplace_back("wallpaper.background: expected #rrggbb; ignored");
            }
        }
        if (const auto feather = wallpaper.find("feather"); feather != wallpaper.end()) {
            if (feather->is_number() && feather->get<double>() >= 0.0 && feather->get<double>() <= 0.25) {
                theme.wallpaper.feather = feather->get<double>();
            } else {
                result.warnings.emplace_back("wallpaper.feather: expected a number between 0 and 0.25; ignored");
            }
        }
    }

    result.theme = std::move(theme);
    return result;
}

ThemeLoadResult loadTheme(const std::filesystem::path& themesRoot, std::string_view id)
{
    if (!isValidItemId(id)) {
        return {std::nullopt, {"invalid theme name"}};
    }
    const auto directory = themesRoot / std::filesystem::path(std::string(id));
    const auto text = readTextFile(directory / "theme.json", 256 * 1024);
    if (!text) {
        return {std::nullopt, {"cannot read " + (directory / "theme.json").string()}};
    }
    return parseTheme(*text, directory);
}

}  // namespace mdu
