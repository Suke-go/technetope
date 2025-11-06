#include "app.hpp"

#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    std::string config_path = "config/control_server.json";
    if (argc > 1) {
        config_path = argv[1];
    } else if (!std::filesystem::exists(config_path)) {
        config_path = "config/control_server.example.json";
    }
    return toio::control::run(config_path);
}
