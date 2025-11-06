#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include "cube_registry.hpp"
#include "relay_connection.hpp"
#include "util/config_loader.hpp"

namespace toio::control {

struct RelayStatusEvent {
    std::string relay_id;
    std::string status;
    std::string message;
};

class RelayManager {
public:
    struct ManualDriveCommand {
        std::vector<std::string> targets;
        int left{0};
        int right{0};
    };

    struct LedCommand {
        std::vector<std::string> targets;
        int r{0};
        int g{0};
        int b{0};
    };

    using StatusCallback = std::function<void(const RelayStatusEvent&)>;
    using CubeUpdateCallback = std::function<void(const std::vector<CubeRegistry::CubeState>&)>;
    using LogCallback = std::function<void(const std::string& level, const std::string& message, const nlohmann::json& context)>;

    RelayManager(boost::asio::io_context& io_context, CubeRegistry& registry, const ControlServerConfig& config);

    void start();
    void stop();

    bool send_manual_drive(const ManualDriveCommand& command, std::string* error_message = nullptr);
    bool send_led_command(const LedCommand& command, std::string* error_message = nullptr);

    void set_status_callback(StatusCallback cb);
    void set_cube_update_callback(CubeUpdateCallback cb);
    void set_log_callback(LogCallback cb);

private:
    struct RelayHandle {
        RelayConfig config;
        std::shared_ptr<RelayConnection> connection;
    };

    void handle_message(const std::string& relay_id, const nlohmann::json& message);
    void handle_status(const std::string& relay_id, RelayConnectionState state, const std::string& message);
    void bootstrap_relay(const RelayHandle& handle);
    bool send_to_cube(const std::string& cube_id, const nlohmann::json& payload, std::string* error_message);
    bool ensure_target_available(const std::string& cube_id, std::string* error_message);

    boost::asio::io_context& io_context_;
    CubeRegistry& registry_;
    std::chrono::milliseconds reconnect_delay_;
    std::unordered_map<std::string, RelayHandle> relays_;
    std::unordered_map<std::string, std::string> cube_to_relay_;
    std::unordered_map<std::string, RelayConnectionState> relay_states_;
    StatusCallback status_callback_;
    CubeUpdateCallback cube_callback_;
    LogCallback log_callback_;
};

}  // namespace toio::control
