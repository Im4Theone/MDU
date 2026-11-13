#pragma once

#include "settings/config.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Messages exchanged between the WebView2 frontend and the native backend.
//
// The frontend is treated as untrusted input: it can only name actions from a
// fixed list, and anything it references (for example an item to launch) is
// looked up in native configuration. It can never supply paths or commands.
namespace mdu::bridge {

struct ReadyMessage {};
struct LaunchMessage {
    std::string itemId;
};

using IncomingMessage = std::variant<ReadyMessage, LaunchMessage>;

constexpr std::size_t maxIncomingMessageSize = 4096;

std::optional<IncomingMessage> parseIncoming(std::string_view json);

struct DockItemView {
    std::string id;
    std::string name;
    std::string icon;  // data: URL, or empty to let the frontend draw a fallback
};

struct DockViewState {
    DockEdge edge = DockEdge::Bottom;
    int iconSize = 48;  // DIPs; the remaining sizes come from DockMetrics so both sides agree
    int padding = 10;
    int gap = 8;
    int shadowInset = 8;
    MotionPreference motion = MotionPreference::System;
    std::vector<std::pair<std::string, std::string>> themeVariables;
    std::string themeStylesheet;  // URL on the trusted virtual host, or empty
    std::vector<DockItemView> items;
};

std::string buildStateMessage(const DockViewState& state);

}  // namespace mdu::bridge
