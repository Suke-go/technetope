#include "util/config_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace toio::control {

namespace {

[[noreturn]] void throw_config_error(const std::filesystem::path& path, const std::string& message) {
    std::ostringstream oss;
    oss << "Config error in " << path << ": " << message;
    throw std::runtime_error(oss.str());
}

}  // namespace

ControlServerConfig load_config(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw_config_error(path, "unable to open file");
    }

    nlohmann::json doc;
    try {
        ifs >> doc;
    } catch (const std::exception& ex) {
        throw_config_error(path, std::string("invalid JSON: ") + ex.what());
    }

    ControlServerConfig config;

    if (!doc.contains("ui")) {
        throw_config_error(path, "missing ui settings");
    }
    const auto& ui = doc.at("ui");
    config.ui.host = ui.value("host", std::string("0.0.0.0"));
    if (!ui.contains("port")) {
        throw_config_error(path, "ui.port is required");
    }
    config.ui.port = ui.at("port").get<std::uint16_t>();
    if (config.ui.port == 0) {
        throw_config_error(path, "ui.port must be > 0");
    }

    if (!doc.contains("relays")) {
        throw_config_error(path, "missing relays list");
    }
    const auto& relays = doc.at("relays");
    if (!relays.is_array() || relays.empty()) {
        throw_config_error(path, "relays must be a non-empty array");
    }

    std::unordered_set<std::string> relay_ids;
    std::unordered_set<std::string> cube_ids;

    for (const auto& relay_json : relays) {
        RelayConfig relay;
        relay.id = relay_json.value("id", std::string{});
        if (relay.id.empty()) {
            throw_config_error(path, "relay entry missing id");
        }
        relay.uri = relay_json.value("uri", std::string{});
        if (relay.uri.empty()) {
            throw_config_error(path, "relay " + relay.id + " missing uri");
        }

        const auto& cubes = relay_json.at("cubes");
        if (!cubes.is_array() || cubes.empty()) {
            throw_config_error(path, "relay " + relay.id + " must define at least one cube");
        }
        for (const auto& cube : cubes) {
            auto cube_id = cube.get<std::string>();
            if (cube_id.size() != 3) {
                throw_config_error(path, "cube id " + cube_id + " must be 3 characters");
            }
            relay.cubes.push_back(cube_id);
            if (!cube_ids.insert(cube_id).second) {
                throw_config_error(path, "cube id " + cube_id + " assigned to multiple relays");
            }
        }

        if (!relay_ids.insert(relay.id).second) {
            throw_config_error(path, "duplicate relay id " + relay.id);
        }

        config.relays.push_back(std::move(relay));
    }

    if (doc.contains("field")) {
        const auto& field_json = doc.at("field");
        if (!field_json.is_object()) {
            throw_config_error(path, "field must be an object with top_left/bottom_right");
        }

        auto parse_point = [&](const nlohmann::json& obj, const std::string& key) -> FieldPoint {
            FieldPoint point;
            const auto& point_json = obj.at(key);
            if (!point_json.is_object()) {
                throw_config_error(path, "field." + key + " must be an object");
            }
            if (!point_json.contains("x") || !point_json.contains("y")) {
                throw_config_error(path, "field." + key + " must contain x and y");
            }
            point.x = point_json.at("x").get<double>();
            point.y = point_json.at("y").get<double>();
            return point;
        };

        if (field_json.contains("top_left")) {
            config.field.top_left = parse_point(field_json, "top_left");
        }
        if (field_json.contains("bottom_right")) {
            config.field.bottom_right = parse_point(field_json, "bottom_right");
        }
    }

    if (config.field.bottom_right.x <= config.field.top_left.x ||
        config.field.bottom_right.y <= config.field.top_left.y) {
        throw_config_error(path, "field.bottom_right must be greater than top_left");
    }

    if (doc.contains("relay_reconnect_ms")) {
        config.relay_reconnect_ms = doc.at("relay_reconnect_ms").get<std::uint32_t>();
    }

    return config;
}

}  // namespace toio::control
