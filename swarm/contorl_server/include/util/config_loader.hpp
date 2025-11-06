#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace toio::control {

struct UiConfig {
    std::string host{"0.0.0.0"};
    std::uint16_t port{0};
};

struct RelayConfig {
    std::string id;
    std::string uri;
    std::vector<std::string> cubes;
};

struct FieldPoint {
    double x{0.0};
    double y{0.0};
};

struct FieldConfig {
    FieldPoint top_left{45.0, 45.0};
    FieldPoint bottom_right{455.0, 455.0};
};

struct ControlServerConfig {
    UiConfig ui;
    std::vector<RelayConfig> relays;
    FieldConfig field{};
    std::uint32_t relay_reconnect_ms{2000};
};

ControlServerConfig load_config(const std::filesystem::path& path);

}  // namespace toio::control
