#include "test_main.h"
#include "ui/bridge.h"

#include <nlohmann/json.hpp>

using namespace mdu;

TEST_CASE("bridge: accepts known messages")
{
    CHECK(std::holds_alternative<bridge::ReadyMessage>(*bridge::parseIncoming(R"({"type":"ready"})")));

    const auto launch = bridge::parseIncoming(R"({"type":"launch","id":"explorer"})");
    CHECK(launch.has_value());
    CHECK_EQ(std::get<bridge::LaunchMessage>(*launch).itemId, std::string("explorer"));
}

TEST_CASE("bridge: rejects malformed and unknown messages")
{
    CHECK(!bridge::parseIncoming(""));
    CHECK(!bridge::parseIncoming("not json"));
    CHECK(!bridge::parseIncoming("[]"));
    CHECK(!bridge::parseIncoming(R"({"kind":"ready"})"));
    CHECK(!bridge::parseIncoming(R"({"type":"exec","command":"cmd.exe /c del"})"));
    CHECK(!bridge::parseIncoming(R"({"type":"launch"})"));
    CHECK(!bridge::parseIncoming(R"({"type":"launch","id":42})"));
    CHECK(!bridge::parseIncoming(R"({"type":"launch","id":"C:\\Windows\\System32\\cmd.exe"})"));
    CHECK(!bridge::parseIncoming(R"({"type":"launch","id":"../x"})"));
}

TEST_CASE("bridge: rejects oversized messages")
{
    std::string big = R"({"type":"ready","padding":")";
    big.append(bridge::maxIncomingMessageSize, 'x');
    big += "\"}";
    CHECK(!bridge::parseIncoming(big));
}

TEST_CASE("bridge: state message contains dock, theme and items")
{
    bridge::DockViewState state;
    state.edge = DockEdge::Left;
    state.iconSize = 40;
    state.motion = MotionPreference::Reduced;
    state.themeVariables = {{"accent", "#8fae3c"}};
    state.themeStylesheet = "https://themes.mdu.example/miku/theme.css";
    state.items = {{"explorer", "File Explorer", "data:image/png;base64,AAAA"}, {"bad", "Bad \xFF name", ""}};

    const auto message = nlohmann::json::parse(bridge::buildStateMessage(state));
    CHECK_EQ(message["type"], "state");
    CHECK_EQ(message["dock"]["edge"], "left");
    CHECK_EQ(message["dock"]["iconSize"], 40);
    CHECK_EQ(message["motion"], "reduced");
    CHECK_EQ(message["theme"]["variables"]["accent"], "#8fae3c");
    CHECK_EQ(message["items"].size(), std::size_t{2});
    CHECK_EQ(message["items"][0]["id"], "explorer");
}
