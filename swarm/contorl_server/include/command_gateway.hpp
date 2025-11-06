#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "cube_registry.hpp"
#include "fleet_orchestrator.hpp"
#include "relay_manager.hpp"
#include "ws_server.hpp"

namespace toio::control {

class CommandGateway {
public:
    CommandGateway(WsServer& ws_server, RelayManager& relay_manager, CubeRegistry& registry, FleetOrchestrator& orchestrator,
                   FieldConfig field_config);

    void handle_open(WsServer::SessionId session_id);
    void handle_close(WsServer::SessionId session_id);
    void handle_message(const nlohmann::json& message, WsServer::SessionId session_id);

    void publish_relay_status(const RelayStatusEvent& event);
    void publish_cube_updates(const std::vector<CubeRegistry::CubeState>& updates);
    void publish_log(const std::string& level, const std::string& message, const nlohmann::json& context);
    void publish_fleet_state();

private:
    struct Subscription {
        std::unordered_set<std::string> streams;
        std::unordered_set<std::string> cube_filter;
    };

    void handle_subscribe(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);
    void handle_manual_drive(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);
    void handle_set_led(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);
    void handle_set_goal(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);
    void handle_set_group(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);
    void handle_request_snapshot(const nlohmann::json& payload, const std::string& request_id, WsServer::SessionId session_id);

    void send_ack(WsServer::SessionId session_id, const std::string& request_id, const nlohmann::json& details = {});
    void send_error(WsServer::SessionId session_id, const std::string& request_id, const std::string& code,
                    const std::string& message);
    void send_snapshot(WsServer::SessionId session_id, bool include_history);
    void publish_field_info(WsServer::SessionId session_id);
    nlohmann::json make_field_payload() const;

    bool stream_enabled(WsServer::SessionId session_id, std::string_view stream) const;
    bool cube_allowed(WsServer::SessionId session_id, const std::string& cube_id) const;
    static std::int64_t now_ms();
    static nlohmann::json cube_state_to_json(const CubeRegistry::CubeState& state);

    WsServer& ws_server_;
    RelayManager& relay_manager_;
    CubeRegistry& registry_;
    FleetOrchestrator& orchestrator_;
    FieldConfig field_config_;
    std::unordered_map<WsServer::SessionId, Subscription> subscriptions_;
    std::unordered_map<std::string, RelayStatusEvent> relay_status_;
    std::unordered_map<std::string, std::vector<std::string>> groups_;
};

}  // namespace toio::control
