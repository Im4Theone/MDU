#include "settings/config.h"
#include "test_main.h"
#include "test_paths.h"

using namespace mdu;

TEST_CASE("config: empty object yields defaults")
{
    const auto result = parseConfig("{}");
    CHECK(result.warnings.empty());
    CHECK(!result.fileWasInvalid);
    CHECK(result.config.startWithWindows);
    CHECK_EQ(result.config.theme, std::string("miku"));
    CHECK_EQ(result.config.dock.iconSize, 48);
    CHECK_EQ(result.config.pinned.size(), defaultConfig().pinned.size());
}

TEST_CASE("config: invalid JSON is reported and defaults are used")
{
    const auto result = parseConfig("{ \"dock\": ");
    CHECK(result.fileWasInvalid);
    CHECK(!result.warnings.empty());
    CHECK_EQ(result.config.dock.edge, DockEdge::Bottom);
}

TEST_CASE("config: valid values are read, invalid ones fall back with warnings")
{
    const auto result = parseConfig(R"({
        // comments are allowed for hand-edited files
        "startWithWindows": false,
        "motion": "reduced",
        "dock": { "edge": "left", "iconSize": 500, "placement": "overlay", "monitors": 3 },
        "wallpaper": { "enabled": true, "image": "C:\\Pictures\\miku.jpg" }
    })");
    CHECK(!result.config.startWithWindows);
    CHECK_EQ(result.config.motion, MotionPreference::Reduced);
    CHECK_EQ(result.config.dock.edge, DockEdge::Left);
    CHECK_EQ(result.config.dock.placement, DockPlacement::Overlay);
    CHECK_EQ(result.config.dock.iconSize, 48);                 // out of range
    CHECK_EQ(result.config.dock.monitors, DockMonitors::All);  // wrong type
    CHECK(result.config.wallpaper.enabled);
    CHECK_EQ(result.config.wallpaper.image, std::string("C:\\Pictures\\miku.jpg"));
    CHECK_EQ(result.warnings.size(), std::size_t{2});
}

TEST_CASE("config: pinned items are validated and deduplicated")
{
    const auto result = parseConfig(R"({ "pinned": [
        { "id": "code", "name": "VS Code", "target": "code.exe" },
        { "id": "code", "name": "Duplicate", "target": "x.exe" },
        { "id": "Bad Id", "target": "x.exe" },
        { "id": "empty-target", "target": "" },
        { "id": "noname", "target": "y.exe", "arguments": "--flag" },
        42
    ]})");
    CHECK_EQ(result.config.pinned.size(), std::size_t{2});
    CHECK_EQ(result.config.pinned[0].id, std::string("code"));
    CHECK_EQ(result.config.pinned[1].name, std::string("noname"));
    CHECK_EQ(result.config.pinned[1].arguments, std::string("--flag"));
    CHECK_EQ(result.warnings.size(), std::size_t{4});
}

TEST_CASE("config: invalid theme names are rejected")
{
    CHECK_EQ(parseConfig(R"({"theme": "../../evil"})").config.theme, std::string("miku"));
    CHECK_EQ(parseConfig(R"({"theme": "midnight"})").config.theme, std::string("midnight"));
}

TEST_CASE("config: save and load round-trip")
{
    const auto directory = test::makeTemporaryDirectory("config");
    const auto path = directory / "nested" / "config.json";

    Config config = defaultConfig();
    config.startWithWindows = false;
    config.dock.edge = DockEdge::Right;
    config.dock.monitors = DockMonitors::Primary;
    config.dock.iconSize = 64;
    config.pinned.push_back({"term", "Terminal \xE2\x9C\x93", "wt.exe", "-p Ubuntu"});

    std::string error;
    CHECK(saveConfigFile(path, config, error));
    CHECK(error.empty());

    const auto loaded = loadConfigFile(path);
    CHECK(loaded.warnings.empty());
    CHECK(!loaded.config.startWithWindows);
    CHECK_EQ(loaded.config.dock.edge, DockEdge::Right);
    CHECK_EQ(loaded.config.dock.monitors, DockMonitors::Primary);
    CHECK_EQ(loaded.config.dock.iconSize, 64);
    CHECK_EQ(loaded.config.pinned.size(), config.pinned.size());
    CHECK_EQ(loaded.config.pinned.back().name, std::string("Terminal \xE2\x9C\x93"));
    CHECK_EQ(loaded.config.pinned.back().arguments, std::string("-p Ubuntu"));
    CHECK(!std::filesystem::exists(path.string() + ".tmp"));

    std::filesystem::remove_all(directory);
}

TEST_CASE("config: missing file yields defaults without warnings")
{
    const auto result = loadConfigFile(test::makeTemporaryDirectory("missing") / "nope.json");
    CHECK(result.warnings.empty());
    CHECK(!result.fileWasInvalid);
}

TEST_CASE("config: unique item ids from display names")
{
    std::vector<PinnedItem> items{{"visual-studio-code", "VS Code", "code.exe", ""}};
    CHECK_EQ(makeUniqueItemId("Visual Studio Code", items), std::string("visual-studio-code-2"));
    CHECK_EQ(makeUniqueItemId("Notepad++", items), std::string("notepad"));
    CHECK_EQ(makeUniqueItemId("  ", items), std::string("item"));
    CHECK_EQ(makeUniqueItemId("\xE3\x83\x9F\xE3\x82\xAF", items), std::string("item"));  // non-ASCII name
    CHECK(isValidItemId(makeUniqueItemId(std::string(200, 'a'), items)));
}
