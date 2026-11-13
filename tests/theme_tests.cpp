#include "test_main.h"
#include "test_paths.h"
#include "ui/theme.h"

#include <fstream>

using namespace mdu;

TEST_CASE("theme: CSS validation")
{
    CHECK(isValidCssVariableName("accent"));
    CHECK(isValidCssVariableName("dock-radius-2"));
    CHECK(!isValidCssVariableName("Accent"));
    CHECK(!isValidCssVariableName("-accent"));
    CHECK(!isValidCssVariableName("a;b"));

    CHECK(isValidCssVariableValue("#8fae3c"));
    CHECK(isValidCssVariableValue("rgba(18, 20, 16, 0.72)"));
    CHECK(isValidCssVariableValue("\"Segoe UI Variable\", system-ui, sans-serif"));
    CHECK(isValidCssVariableValue("cubic-bezier(0.2, 0, 0, 1)"));
    CHECK(!isValidCssVariableValue("red; } body { display: none"));
    CHECK(!isValidCssVariableValue("URL(https://example.com/x.png)"));
    CHECK(!isValidCssVariableValue("</style>"));
    CHECK(!isValidCssVariableValue("\\66"));
    CHECK(!isValidCssVariableValue(""));
}

TEST_CASE("theme: relative path validation")
{
    CHECK(isSafeRelativePath("theme.css"));
    CHECK(isSafeRelativePath("wallpaper/mobile.jpg"));
    CHECK(!isSafeRelativePath("../secret.txt"));
    CHECK(!isSafeRelativePath("a/../../b"));
    CHECK(!isSafeRelativePath("C:\\Windows\\win.ini"));
    CHECK(!isSafeRelativePath("/etc/passwd"));
    CHECK(!isSafeRelativePath("\\\\server\\share"));
    CHECK(!isSafeRelativePath("a//b"));
    CHECK(!isSafeRelativePath("file.css:stream"));
}

TEST_CASE("theme: hex colours")
{
    CHECK(parseHexColor("#a8a8a8") == ThemeColor{168, 168, 168});
    CHECK(parseHexColor("#8FAE3C") == ThemeColor{0x8f, 0xae, 0x3c});
    CHECK(!parseHexColor("a8a8a8"));
    CHECK(!parseHexColor("#a8a8a"));
    CHECK(!parseHexColor("#zzzzzz"));
}

TEST_CASE("theme: parse keeps valid fields and reports invalid ones")
{
    const auto result = parseTheme(R"json({
        "name": "Test",
        "variables": { "accent": "#8fae3c", "bad;name": "#000000", "surface": "url(x)", "num": 5 },
        "stylesheet": "theme.css",
        "wallpaper": { "image": "../outside.jpg", "background": "#a8a8a8", "feather": 0.06 }
    })json", "/themes/test");

    CHECK(result.theme.has_value());
    const auto& theme = *result.theme;
    CHECK_EQ(theme.id, std::string("test"));
    CHECK_EQ(theme.name, std::string("Test"));
    CHECK_EQ(theme.variables.size(), std::size_t{1});
    CHECK_EQ(theme.stylesheet, std::string("theme.css"));
    CHECK(theme.wallpaper.image.empty());
    CHECK(theme.wallpaper.background == ThemeColor{168, 168, 168});
    CHECK(theme.wallpaper.feather == 0.06);
    CHECK_EQ(result.warnings.size(), std::size_t{4});
}

TEST_CASE("theme: the bundled Miku theme loads without warnings")
{
    const auto result = loadTheme(MDU_SOURCE_DIR "/themes", "miku");
    for (const auto& warning : result.warnings) {
        std::cerr << "  warning: " << warning << "\n";
    }
    CHECK(result.theme.has_value());
    CHECK(result.warnings.empty());
    if (result.theme) {
        CHECK(!result.theme->variables.empty());
        CHECK(!result.theme->stylesheet.empty());
        CHECK(std::filesystem::exists(result.theme->directory / result.theme->stylesheet));
    }
}

TEST_CASE("theme: loading rejects traversal in the theme id")
{
    CHECK(!loadTheme(MDU_SOURCE_DIR "/themes", "../src").theme);
}
