#include "ui/bridge.h"

#include <nlohmann/json.hpp>

namespace mdu::bridge {

namespace {

std::string_view edgeName(DockEdge edge)
{
    switch (edge) {
    case DockEdge::Left: return "left";
    case DockEdge::Right: return "right";
    case DockEdge::Bottom: break;
    }
    return "bottom";
}

std::string_view motionName(MotionPreference motion)
{
    switch (motion) {
    case MotionPreference::Reduced: return "reduced";
    case MotionPreference::Full: return "full";
    case MotionPreference::System: break;
    }
    return "system";
}

}  // namespace

std::optional<IncomingMessage> parseIncoming(std::string_view text)
{
    if (text.empty() || text.size() > maxIncomingMessageSize) {
        return std::nullopt;
    }

    const auto root = nlohmann::json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto type = root.find("type");
    if (type == root.end() || !type->is_string()) {
        return std::nullopt;
    }

    const auto& name = type->get_ref<const std::string&>();
    if (name == "ready") {
        return ReadyMessage{};
    }
    if (name == "launch") {
        const auto id = root.find("id");
        if (id == root.end() || !id->is_string() || !isValidItemId(id->get_ref<const std::string&>())) {
            return std::nullopt;
        }
        return LaunchMessage{id->get<std::string>()};
    }
    return std::nullopt;
}

std::string buildStateMessage(const DockViewState& state)
{
    nlohmann::json variables = nlohmann::json::object();
    for (const auto& [name, value] : state.themeVariables) {
        variables[name] = value;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : state.items) {
        items.push_back({{"id", item.id}, {"name", item.name}, {"icon", item.icon}});
    }

    const nlohmann::json message = {
        {"type", "state"},
        {"dock",
         {{"edge", edgeName(state.edge)},
          {"iconSize", state.iconSize},
          {"padding", state.padding},
          {"gap", state.gap},
          {"shadowInset", state.shadowInset}}},
        {"motion", motionName(state.motion)},
        {"theme", {{"variables", variables}, {"stylesheet", state.themeStylesheet}}},
        {"items", items},
    };
    // Replace invalid UTF-8 instead of throwing; display names come from user files.
    return message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace mdu::bridge
