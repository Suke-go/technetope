#include "relay_manager.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/logging.hpp"

namespace toio::control {

namespace {

std::string to_status_string(RelayConnectionState state) {
    switch (state) {
        case RelayConnectionState::Stopped: return "stopped";
        case RelayConnectionState::Connecting: return "connecting";
        case RelayConnectionState::Connected: return "connected";
    }
    return "unknown";
}

}  // namespace

RelayManager::RelayManager(boost::asio::io_context& io_context, CubeRegistry& registry, const ControlServerConfig& config)
    : io_context_(io_context),
      registry_(registry),
      reconnect_delay_(std::chrono::milliseconds(config.relay_reconnect_ms)) {
    for (const auto& relay_cfg : config.relays) {
        RelayHandle handle;
        handle.config = relay_cfg;
        handle.connection = std::make_shared<RelayConnection>(
            io_context_, RelayConnection::Options{relay_cfg.id, relay_cfg.uri, reconnect_delay_});

        const auto relay_id = relay_cfg.id;
        handle.connection->set_message_handler(
            [this, relay_id](const nlohmann::json& message) { handle_message(relay_id, message); });
        handle.connection->set_status_handler([this, relay_id](RelayConnectionState state, const std::string& msg) {
            handle_status(relay_id, state, msg);
        });

        relays_.emplace(relay_id, std::move(handle));
        relay_states_.emplace(relay_id, RelayConnectionState::Stopped);

        for (const auto& cube : relay_cfg.cubes) {
            cube_to_relay_[cube] = relay_id;
        }
    }
}

void RelayManager::start() {
    for (auto& [_, handle] : relays_) {
        handle.connection->start();
    }
}

void RelayManager::stop() {
    for (auto& [_, handle] : relays_) {
        handle.connection->stop();
    }
}

void RelayManager::set_status_callback(StatusCallback cb) {
    status_callback_ = std::move(cb);
}

void RelayManager::set_cube_update_callback(CubeUpdateCallback cb) {
    cube_callback_ = std::move(cb);
}

void RelayManager::set_log_callback(LogCallback cb) {
    log_callback_ = std::move(cb);
}

bool RelayManager::send_manual_drive(const ManualDriveCommand& command, std::string* error_message) {
    if (command.targets.empty()) {
        if (error_message) {
            *error_message = "manual_drive requires at least one target";
        }
        return false;
    }

    for (const auto& target : command.targets) {
        nlohmann::json payload{
            {"type", "command"},
            {"payload",
             {
                 {"cmd", "move"},
                 {"target", target},
                 {"params", {{"left_speed", command.left}, {"right_speed", command.right}}},
                 {"require_result", false},
             }},
        };
        if (!send_to_cube(target, payload, error_message)) {
            return false;
        }
    }
    return true;
}

bool RelayManager::send_led_command(const LedCommand& command, std::string* error_message) {
    if (command.targets.empty()) {
        if (error_message) {
            *error_message = "set_led requires at least one target";
        }
        return false;
    }

    for (const auto& target : command.targets) {
        nlohmann::json payload{
            {"type", "command"},
            {"payload",
             {
                 {"cmd", "led"},
                 {"target", target},
                 {"params", {{"r", command.r}, {"g", command.g}, {"b", command.b}}},
                 {"require_result", false},
             }},
        };
        if (!send_to_cube(target, payload, error_message)) {
            return false;
        }
    }
    return true;
}

bool RelayManager::send_to_cube(const std::string& cube_id, const nlohmann::json& payload, std::string* error_message) {
    std::string relay_id;
    if (!ensure_target_available(cube_id, error_message)) {
        return false;
    }
    relay_id = cube_to_relay_.at(cube_id);
    auto relay_it = relays_.find(relay_id);
    if (relay_it == relays_.end()) {
        if (error_message) {
            *error_message = "relay " + relay_id + " not registered";
        }
        return false;
    }
    if (relay_states_[relay_id] != RelayConnectionState::Connected) {
        if (error_message) {
            *error_message = "relay " + relay_id + " not connected";
        }
        return false;
    }

    relay_it->second.connection->send(payload);
    return true;
}

bool RelayManager::ensure_target_available(const std::string& cube_id, std::string* error_message) {
    if (cube_to_relay_.find(cube_id) == cube_to_relay_.end()) {
        if (error_message) {
            *error_message = "cube " + cube_id + " is not registered";
        }
        return false;
    }
    return true;
}

void RelayManager::handle_status(const std::string& relay_id, RelayConnectionState state, const std::string& message) {
    relay_states_[relay_id] = state;

    RelayStatusEvent event{
        .relay_id = relay_id,
        .status = to_status_string(state),
        .message = message,
    };
    if (status_callback_) {
        status_callback_(event);
    }

    if (state == RelayConnectionState::Connected) {
        const auto it = relays_.find(relay_id);
        if (it != relays_.end()) {
            bootstrap_relay(it->second);
        }
    }
}

void RelayManager::bootstrap_relay(const RelayHandle& handle) {
    for (const auto& cube : handle.config.cubes) {
        nlohmann::json connect_msg{
            {"type", "command"},
            {"payload",
             {
                 {"cmd", "connect"},
                 {"target", cube},
                 {"require_result", false},
             }},
        };
        handle.connection->send(connect_msg);

        nlohmann::json subscribe_msg{
            {"type", "query"},
            {"payload",
             {
                 {"info", "position"},
                 {"target", cube},
                 {"notify", true},
             }},
        };
        handle.connection->send(subscribe_msg);

        nlohmann::json battery_msg{
            {"type", "query"},
            {"payload",
             {
                 {"info", "battery"},
                 {"target", cube},
             }},
        };
        handle.connection->send(battery_msg);
    }
}

void RelayManager::handle_message(const std::string& relay_id, const nlohmann::json& message) {
    const auto type_it = message.find("type");
    if (type_it == message.end() || !type_it->is_string()) {
        return;
    }
    const auto type = type_it->get<std::string>();
    const auto payload_it = message.find("payload");
    const auto now = std::chrono::system_clock::now();

    if (type == "response" && payload_it != message.end() && payload_it->is_object()) {
        const auto info = payload_it->value("info", std::string{});
        if (info == "position") {
            CubeRegistry::Update update;
            update.cube_id = payload_it->value("target", std::string{});
            if (update.cube_id.empty()) {
                return;
            }
            update.relay_id = relay_id;
            update.timestamp = now;
            if (auto pos_it = payload_it->find("position"); pos_it != payload_it->end() && pos_it->is_object()) {
                CubeRegistry::Pose pose;
                bool has_value = false;
                if (auto x_it = pos_it->find("x"); x_it != pos_it->end() && x_it->is_number()) {
                    pose.x = x_it->get<double>();
                    has_value = true;
                }
                if (auto y_it = pos_it->find("y"); y_it != pos_it->end() && y_it->is_number()) {
                    pose.y = y_it->get<double>();
                    has_value = true;
                }
                if (auto angle_it = pos_it->find("angle"); angle_it != pos_it->end() && angle_it->is_number()) {
                    pose.deg = angle_it->get<double>();
                    has_value = true;
                }
                if (auto on_mat_it = pos_it->find("on_mat"); on_mat_it != pos_it->end() && on_mat_it->is_boolean()) {
                    pose.on_mat = on_mat_it->get<bool>();
                    has_value = true;
                }
                if (has_value) {
                    update.position = pose;
                }
            }
            if (auto led_it = payload_it->find("led"); led_it != payload_it->end() && led_it->is_object()) {
                CubeRegistry::LedState led_state;
                bool has_led = false;
                if (auto r_it = led_it->find("r"); r_it != led_it->end() && r_it->is_number_integer()) {
                    led_state.r = r_it->get<int>();
                    has_led = true;
                }
                if (auto g_it = led_it->find("g"); g_it != led_it->end() && g_it->is_number_integer()) {
                    led_state.g = g_it->get<int>();
                    has_led = true;
                }
                if (auto b_it = led_it->find("b"); b_it != led_it->end() && b_it->is_number_integer()) {
                    led_state.b = b_it->get<int>();
                    has_led = true;
                }
                if (has_led) {
                    update.led = led_state;
                }
            }
            std::vector<CubeRegistry::Update> updates;
            updates.push_back(update);
            auto changed = registry_.apply_updates(updates);
            if (!changed.empty() && cube_callback_) {
                cube_callback_(changed);
            }
        } else if (info == "battery") {
            CubeRegistry::Update update;
            update.cube_id = payload_it->value("target", std::string{});
            if (update.cube_id.empty()) {
                return;
            }
            update.relay_id = relay_id;
            update.timestamp = now;
            if (payload_it->contains("battery_level")) {
                const auto& battery_json = payload_it->at("battery_level");
                if (battery_json.is_number_integer()) {
                    update.battery = battery_json.get<int>();
                }
            }
            auto changed = registry_.apply_update(update);
            if (changed && cube_callback_) {
                cube_callback_({*changed});
            }
        }
    } else if (type == "system") {
        if (log_callback_) {
            log_callback_("info", "relay system message",
                          {{"relay_id", relay_id}, {"message", payload_it != message.end() ? *payload_it : nlohmann::json{}}});
        } else {
            util::log::info("Relay " + relay_id + " system message");
        }
    } else if (type == "error") {
        const auto error_message =
            payload_it != message.end() && payload_it->contains("message") ? payload_it->value("message", std::string{}) : "";
        if (log_callback_) {
            log_callback_("error", error_message, {{"relay_id", relay_id}});
        } else {
            util::log::error("Relay " + relay_id + " error: " + error_message);
        }
    }
}

}  // namespace toio::control
