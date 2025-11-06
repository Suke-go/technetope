#include "command_gateway.hpp"

#include <chrono>
#include <stdexcept>

#include "util/logging.hpp"

namespace toio::control {

namespace {

const std::unordered_set<std::string> kDefaultStreams = {"relay_status", "cube_update", "fleet_state", "log"};

}  // namespace

CommandGateway::CommandGateway(WsServer& ws_server, RelayManager& relay_manager, CubeRegistry& registry,
                               FleetOrchestrator& orchestrator, FieldConfig field_config)
    : ws_server_(ws_server),
      relay_manager_(relay_manager),
      registry_(registry),
      orchestrator_(orchestrator),
      field_config_(field_config) {}

void CommandGateway::handle_open(WsServer::SessionId session_id) {
    Subscription subscription;
    subscription.streams = kDefaultStreams;
    subscriptions_[session_id] = std::move(subscription);
    send_snapshot(session_id, false);
}

void CommandGateway::handle_close(WsServer::SessionId session_id) {
    subscriptions_.erase(session_id);
}

void CommandGateway::handle_message(const nlohmann::json& message, WsServer::SessionId session_id) {
    if (!message.contains("type") || !message["type"].is_string()) {
        send_error(session_id, "", "invalid_payload", "message.type must be string");
        return;
    }
    const auto type = message.at("type").get<std::string>();
    const auto request_id = message.value("request_id", std::string{});
    const auto payload = message.value("payload", nlohmann::json::object());

    if (type == "subscribe") {
        handle_subscribe(payload, request_id, session_id);
    } else if (type == "manual_drive") {
        handle_manual_drive(payload, request_id, session_id);
    } else if (type == "set_led") {
        handle_set_led(payload, request_id, session_id);
    } else if (type == "set_goal") {
        handle_set_goal(payload, request_id, session_id);
    } else if (type == "set_group") {
        handle_set_group(payload, request_id, session_id);
    } else if (type == "request_snapshot") {
        handle_request_snapshot(payload, request_id, session_id);
    } else {
        send_error(session_id, request_id, "invalid_payload", "unknown command type: " + type);
    }
}

void CommandGateway::publish_relay_status(const RelayStatusEvent& event) {
    relay_status_[event.relay_id] = event;
    const auto timestamp = now_ms();
    nlohmann::json payload{{"relay_id", event.relay_id}, {"status", event.status}, {"message", event.message}};
    nlohmann::json envelope{{"type", "relay_status"}, {"timestamp", timestamp}, {"payload", payload}};

    for (const auto& [session_id, _] : subscriptions_) {
        if (stream_enabled(session_id, "relay_status")) {
            ws_server_.send(session_id, envelope);
        }
    }
}

void CommandGateway::publish_cube_updates(const std::vector<CubeRegistry::CubeState>& updates) {
    if (updates.empty()) {
        return;
    }
    const auto timestamp = now_ms();
    for (const auto& [session_id, _sub] : subscriptions_) {
        if (!stream_enabled(session_id, "cube_update")) {
            continue;
        }
        nlohmann::json batch = nlohmann::json::array();
        for (const auto& state : updates) {
            if (!cube_allowed(session_id, state.cube_id)) {
                continue;
            }
            batch.push_back(cube_state_to_json(state));
        }
        if (batch.empty()) {
            continue;
        }
        nlohmann::json payload{{"updates", batch}};
        nlohmann::json envelope{{"type", "cube_update"}, {"timestamp", timestamp}, {"payload", payload}};
        ws_server_.send(session_id, envelope);
    }
}

void CommandGateway::publish_log(const std::string& level, const std::string& message, const nlohmann::json& context) {
    const auto timestamp = now_ms();
    nlohmann::json payload{{"level", level}, {"message", message}, {"context", context}};
    nlohmann::json envelope{{"type", "log"}, {"timestamp", timestamp}, {"payload", payload}};
    for (const auto& [session_id, _] : subscriptions_) {
        if (stream_enabled(session_id, "log")) {
            ws_server_.send(session_id, envelope);
        }
    }
}

void CommandGateway::publish_fleet_state() {
    const auto snapshot = orchestrator_.snapshot();
    const auto timestamp = now_ms();
    nlohmann::json payload{
        {"tick_hz", snapshot.tick_hz},
        {"tasks_in_queue", snapshot.tasks_in_queue},
        {"warnings", snapshot.warnings},
    };
    nlohmann::json active = nlohmann::json::array();
    for (const auto& goal : snapshot.active_goals) {
        nlohmann::json goal_json{
            {"goal_id", goal.goal_id},
            {"cube_id", goal.cube_id},
            {"priority", goal.priority},
            {"created_at",
             std::chrono::duration_cast<std::chrono::milliseconds>(goal.created_at.time_since_epoch()).count()},
            {"pose", {{"x", goal.pose.x}, {"y", goal.pose.y}}},
        };
        if (goal.pose.angle) {
            goal_json["pose"]["angle"] = *goal.pose.angle;
        }
        active.push_back(goal_json);
    }
    payload["active_goals"] = active;

    nlohmann::json envelope{{"type", "fleet_state"}, {"timestamp", timestamp}, {"payload", payload}};
    for (const auto& [session_id, _] : subscriptions_) {
        if (stream_enabled(session_id, "fleet_state")) {
            ws_server_.send(session_id, envelope);
        }
    }
}

void CommandGateway::handle_subscribe(const nlohmann::json& payload, const std::string& request_id,
                                      WsServer::SessionId session_id) {
    Subscription subscription;
    subscription.streams = kDefaultStreams;
    subscription.cube_filter.clear();

    if (payload.contains("streams") && payload["streams"].is_array()) {
        subscription.streams.clear();
        for (const auto& item : payload["streams"]) {
            if (item.is_string()) {
                subscription.streams.insert(item.get<std::string>());
            }
        }
        if (subscription.streams.empty()) {
            subscription.streams = kDefaultStreams;
        }
    }

    if (payload.contains("cube_filter") && payload["cube_filter"].is_array()) {
        for (const auto& item : payload["cube_filter"]) {
            if (item.is_string()) {
                subscription.cube_filter.insert(item.get<std::string>());
            }
        }
    }

    const bool include_history = payload.value("include_history", false);
    subscriptions_[session_id] = std::move(subscription);
    send_ack(session_id, request_id);
    publish_field_info(session_id);
    if (include_history) {
        send_snapshot(session_id, true);
    }
}

void CommandGateway::handle_manual_drive(const nlohmann::json& payload, const std::string& request_id,
                                         WsServer::SessionId session_id) {
    RelayManager::ManualDriveCommand command;
    if (!payload.contains("targets") || !payload["targets"].is_array()) {
        send_error(session_id, request_id, "invalid_payload", "manual_drive.targets must be array");
        return;
    }
    for (const auto& target : payload["targets"]) {
        if (target.is_string()) {
            command.targets.push_back(target.get<std::string>());
        }
    }
    command.left = payload.value("left", 0);
    command.right = payload.value("right", 0);

    std::string error;
    if (!relay_manager_.send_manual_drive(command, &error)) {
        send_error(session_id, request_id, error.empty() ? "relay_error" : "relay_error", error);
        return;
    }
    send_ack(session_id, request_id);
}

void CommandGateway::handle_set_led(const nlohmann::json& payload, const std::string& request_id,
                                    WsServer::SessionId session_id) {
    RelayManager::LedCommand command;
    if (!payload.contains("targets") || !payload["targets"].is_array()) {
        send_error(session_id, request_id, "invalid_payload", "set_led.targets must be array");
        return;
    }
    for (const auto& target : payload["targets"]) {
        if (target.is_string()) {
            command.targets.push_back(target.get<std::string>());
        }
    }
    const auto& color = payload.value("color", nlohmann::json::object());
    if (!color.is_object()) {
        send_error(session_id, request_id, "invalid_payload", "color must be object");
        return;
    }
    command.r = color.value("r", 0);
    command.g = color.value("g", 0);
    command.b = color.value("b", 0);

    std::string error;
    if (!relay_manager_.send_led_command(command, &error)) {
        send_error(session_id, request_id, "relay_error", error);
        return;
    }
    const auto now = std::chrono::system_clock::now();
    std::vector<CubeRegistry::Update> updates;
    updates.reserve(command.targets.size());
    for (const auto& cube : command.targets) {
        CubeRegistry::Update update;
        update.cube_id = cube;
        update.timestamp = now;
        update.led = CubeRegistry::LedState{command.r, command.g, command.b};
        updates.push_back(update);
    }
    auto changed = registry_.apply_updates(updates);
    if (!changed.empty()) {
        publish_cube_updates(changed);
    }
    send_ack(session_id, request_id);
}

void CommandGateway::handle_set_goal(const nlohmann::json& payload, const std::string& request_id,
                                     WsServer::SessionId session_id) {
    FleetOrchestrator::GoalRequest request;
    if (!payload.contains("targets") || !payload["targets"].is_array() || payload["targets"].empty()) {
        send_error(session_id, request_id, "invalid_payload", "set_goal.targets must be non-empty array");
        return;
    }
    for (const auto& target : payload["targets"]) {
        if (target.is_string()) {
            request.targets.push_back(target.get<std::string>());
        }
    }
    if (!payload.contains("goal") || !payload["goal"].is_object()) {
        send_error(session_id, request_id, "invalid_payload", "goal must be object");
        return;
    }
    const auto& goal_json = payload["goal"];
    request.pose.x = goal_json.value("x", 0.0);
    request.pose.y = goal_json.value("y", 0.0);
    if (goal_json.contains("angle")) {
        request.pose.angle = goal_json.value("angle", 0.0);
    }
    request.priority = payload.value("priority", 0);
    request.keep_history = payload.value("keep_history", false);

    std::string goal_id;
    try {
        goal_id = orchestrator_.assign_goal(request);
    } catch (const std::exception& ex) {
        send_error(session_id, request_id, "invalid_payload", ex.what());
        return;
    }

    std::vector<CubeRegistry::Update> updates;
    const auto now = std::chrono::system_clock::now();
    for (const auto& cube : request.targets) {
        CubeRegistry::Update update;
        update.cube_id = cube;
        update.relay_id = "";
        update.goal_id = goal_id;
        update.timestamp = now;
        updates.push_back(update);
    }
    auto changed = registry_.apply_updates(updates);
    if (!changed.empty()) {
        publish_cube_updates(changed);
    }
    publish_fleet_state();
    send_ack(session_id, request_id, {{"goal_id", goal_id}});
}

void CommandGateway::handle_set_group(const nlohmann::json& payload, const std::string& request_id,
                                      WsServer::SessionId session_id) {
    const auto group_id = payload.value("group_id", std::string{});
    if (group_id.empty()) {
        send_error(session_id, request_id, "invalid_payload", "group_id is required");
        return;
    }
    if (!payload.contains("members") || !payload["members"].is_array()) {
        send_error(session_id, request_id, "invalid_payload", "members must be array");
        return;
    }
    std::vector<std::string> members;
    for (const auto& member : payload["members"]) {
        if (member.is_string()) {
            members.push_back(member.get<std::string>());
        }
    }
    groups_[group_id] = members;
    send_ack(session_id, request_id);
}

void CommandGateway::handle_request_snapshot(const nlohmann::json& payload, const std::string& request_id,
                                             WsServer::SessionId session_id) {
    const bool include_history = payload.value("include_history", false);
    send_snapshot(session_id, include_history);
    send_ack(session_id, request_id);
}

void CommandGateway::send_ack(WsServer::SessionId session_id, const std::string& request_id,
                              const nlohmann::json& details) {
    nlohmann::json payload{{"request_id", request_id}};
    if (!details.is_null() && !details.empty()) {
        payload["details"] = details;
    }
    ws_server_.send(session_id, {{"type", "ack"}, {"timestamp", now_ms()}, {"payload", payload}});
}

void CommandGateway::send_error(WsServer::SessionId session_id, const std::string& request_id, const std::string& code,
                                const std::string& message) {
    nlohmann::json payload{{"request_id", request_id}, {"code", code}, {"message", message}};
    ws_server_.send(session_id, {{"type", "error"}, {"timestamp", now_ms()}, {"payload", payload}});
}

void CommandGateway::send_snapshot(WsServer::SessionId session_id, bool include_history) {
    nlohmann::json relays = nlohmann::json::array();
    for (const auto& [relay_id, status] : relay_status_) {
        relays.push_back({{"relay_id", relay_id}, {"status", status.status}, {"message", status.message}});
    }
    auto cubes = registry_.snapshot();
    nlohmann::json cube_array = nlohmann::json::array();
    for (const auto& cube : cubes) {
        cube_array.push_back(cube_state_to_json(cube));
    }
    nlohmann::json history = nlohmann::json::array();
    if (include_history) {
        const auto entries = registry_.history(64);
        for (const auto& entry : entries) {
            auto json = cube_state_to_json(entry.state);
            json["timestamp"] =
                std::chrono::duration_cast<std::chrono::milliseconds>(entry.timestamp.time_since_epoch()).count();
            history.push_back(json);
        }
    }
    nlohmann::json payload{
        {"field", make_field_payload()},
        {"relays", relays},
        {"cubes", cube_array},
        {"history", history},
    };
    ws_server_.send(session_id, {{"type", "snapshot"}, {"timestamp", now_ms()}, {"payload", payload}});
}

void CommandGateway::publish_field_info(WsServer::SessionId session_id) {
    ws_server_.send(
        session_id,
        {{"type", "field_info"}, {"timestamp", now_ms()}, {"payload", make_field_payload()}});
}

bool CommandGateway::stream_enabled(WsServer::SessionId session_id, std::string_view stream) const {
    const auto it = subscriptions_.find(session_id);
    if (it == subscriptions_.end()) {
        return true;
    }
    return it->second.streams.empty() || it->second.streams.count(std::string(stream)) > 0;
}

bool CommandGateway::cube_allowed(WsServer::SessionId session_id, const std::string& cube_id) const {
    const auto it = subscriptions_.find(session_id);
    if (it == subscriptions_.end()) {
        return true;
    }
    if (it->second.cube_filter.empty()) {
        return true;
    }
    return it->second.cube_filter.count(cube_id) > 0;
}

std::int64_t CommandGateway::now_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

nlohmann::json CommandGateway::cube_state_to_json(const CubeRegistry::CubeState& state) {
    nlohmann::json json{
        {"cube_id", state.cube_id},
        {"relay_id", state.relay_id},
        {"battery", state.battery},
        {"state", state.state},
        {"goal_id", state.goal_id},
    };
    if (state.has_position) {
        json["position"] = {{"x", state.position.x},
                            {"y", state.position.y},
                            {"deg", state.position.deg},
                            {"on_mat", state.position.on_mat}};
    }
    json["led"] = {{"r", state.led.r}, {"g", state.led.g}, {"b", state.led.b}};
    return json;
}

nlohmann::json CommandGateway::make_field_payload() const {
    return {{"top_left", {{"x", field_config_.top_left.x}, {"y", field_config_.top_left.y}}},
            {"bottom_right", {{"x", field_config_.bottom_right.x}, {"y", field_config_.bottom_right.y}}}};
}

}  // namespace toio::control
