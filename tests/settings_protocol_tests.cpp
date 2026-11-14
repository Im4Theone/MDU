#include "test_main.h"
#include "ui/settings_protocol.h"

#include <nlohmann/json.hpp>

using namespace mdu;
namespace sp = settings_protocol;

namespace {

const std::vector<std::string> themes{"miku", "midnight"};

Config current()
{
    Config config = defaultConfig();  // explorer, settings, notepad
    config.wallpaper.image = "C:\\Pictures\\miku.jpg";
    return config;
}

std::optional<Config> change(std::string_view json)
{
    const auto message = sp::parse(json, current(), themes);
    if (!message || !std::holds_alternative<sp::ConfigChange>(*message)) {
        return std::nullopt;
    }
    return std::get<sp::ConfigChange>(*message).config;
}

}  // namespace

TEST_CASE("settings protocol: simple requests")
{
    CHECK(std::holds_alternative<sp::Ready>(*sp::parse(R"({"type":"settings-ready"})", current(), themes)));
    CHECK(std::holds_alternative<sp::AddItem>(*sp::parse(R"({"type":"settings-add-item"})", current(), themes)));
    CHECK(std::holds_alternative<sp::ChooseWallpaper>(
        *sp::parse(R"({"type":"settings-choose-wallpaper"})", current(), themes)));
    CHECK(std::holds_alternative<sp::OpenSettingsFile>(
        *sp::parse(R"({"type":"settings-open-file"})", current(), themes)));
    CHECK(!sp::parse(R"({"type":"launch","id":"explorer"})", current(), themes));  // dock message
    CHECK(!sp::parse("nonsense", current(), themes));
}

TEST_CASE("settings protocol: valid update changes only the given values")
{
    const auto next = change(R"({"type":"settings-update","settings":{
        "startWithWindows": false, "theme": "midnight", "motion": "reduced",
        "dock": {"edge": "left", "iconSize": 64}, "wallpaper": {"enabled": true}}})");
    const Config before = current();
    CHECK(next.has_value());
    if (next) {
        CHECK(!next->startWithWindows);
        CHECK_EQ(next->theme, std::string("midnight"));
        CHECK_EQ(next->motion, MotionPreference::Reduced);
        CHECK_EQ(next->dock.edge, DockEdge::Left);
        CHECK_EQ(next->dock.iconSize, 64);
        CHECK_EQ(next->dock.margin, before.dock.margin);
        CHECK(next->wallpaper.enabled);
        CHECK_EQ(next->wallpaper.image, before.wallpaper.image);  // untouched
        CHECK_EQ(next->pinned.size(), before.pinned.size());
    }
}

TEST_CASE("settings protocol: updates with bad values or extra keys are rejected")
{
    CHECK(!change(R"({"type":"settings-update","settings":{"dock":{"iconSize":4096}}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"dock":{"edge":"top"}}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"dock":{"iconSize":"64"}}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"theme":"not-installed"}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"theme":"../../evil"}})"));
    // Paths and commands can never come from the page.
    CHECK(!change(R"({"type":"settings-update","settings":{"wallpaper":{"image":"C:\\evil.jpg"}}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"pinned":[{"id":"x","target":"cmd.exe"}]}})"));
    CHECK(!change(R"({"type":"settings-update","settings":{"dock":{"enabled":true,"command":"calc"}}})"));
    CHECK(!change(R"({"type":"settings-update","settings":"everything"})"));
    CHECK(!change(R"({"type":"settings-update"})"));
}

TEST_CASE("settings protocol: items can be reordered and removed but not invented")
{
    const Config before = current();
    const auto reordered = change(R"({"type":"settings-items","order":["notepad","explorer"]})");
    CHECK(reordered.has_value());
    if (reordered) {
        CHECK_EQ(reordered->pinned.size(), std::size_t{2});
        CHECK_EQ(reordered->pinned[0].id, std::string("notepad"));
        CHECK_EQ(reordered->pinned[0].target, before.pinned[2].target);  // target kept natively
    }
    CHECK(change(R"({"type":"settings-items","order":[]})").has_value());
    CHECK(!change(R"({"type":"settings-items","order":["explorer","explorer"]})"));
    CHECK(!change(R"({"type":"settings-items","order":["unknown"]})"));
    CHECK(!change(R"({"type":"settings-items","order":[{"id":"x","target":"cmd.exe"}]})"));
    CHECK(!change(R"({"type":"settings-items","order":"explorer"})"));
}

TEST_CASE("settings protocol: rename validates the item and the name")
{
    const auto renamed = change(R"({"type":"settings-rename","id":"explorer","name":"Files"})");
    CHECK(renamed.has_value());
    if (renamed) {
        CHECK_EQ(renamed->pinned[0].name, std::string("Files"));
    }
    CHECK(!change(R"({"type":"settings-rename","id":"nope","name":"Files"})"));
    CHECK(!change(R"({"type":"settings-rename","id":"explorer","name":""})"));
    CHECK(!change(R"({"type":"settings-rename","id":"explorer","name":"bad\nname"})"));
    CHECK(!change(R"({"type":"settings-rename","id":"explorer"})"));
}

TEST_CASE("settings protocol: reset wallpaper clears the custom image")
{
    const auto reset = change(R"({"type":"settings-reset-wallpaper"})");
    CHECK(reset.has_value());
    if (reset) {
        CHECK(reset->wallpaper.image.empty());
    }
}

TEST_CASE("settings protocol: state message")
{
    sp::StateView view;
    view.version = "0.1.0";
    view.themes = {{"miku", "Miku"}};
    view.icons = {{"explorer", "data:image/png;base64,AA"}};
    view.wallpaperImageName = "miku.jpg";
    view.themeVariables = {{"accent", "#86a83c"}};
    const auto state = nlohmann::json::parse(sp::buildState(current(), view));
    CHECK_EQ(state["type"], "settings-state");
    CHECK_EQ(state["settings"]["dock"]["edge"], "bottom");
    CHECK(!state["settings"].contains("pinned"));
    CHECK(!state["settings"]["wallpaper"].contains("image"));  // full path stays native
    CHECK_EQ(state["wallpaperImage"], "miku.jpg");
    CHECK_EQ(state["items"].size(), std::size_t{3});
    CHECK_EQ(state["items"][0]["icon"], "data:image/png;base64,AA");
    CHECK_EQ(state["themes"][0]["name"], "Miku");
    CHECK_EQ(state["theme"]["variables"]["accent"], "#86a83c");
}
