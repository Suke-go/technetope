#pragma once

#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "command_gateway.hpp"
#include "cube_registry.hpp"
#include "fleet_orchestrator.hpp"
#include "relay_manager.hpp"
#include "util/config_loader.hpp"
#include "ws_server.hpp"

namespace toio::control {

class ControlServerApp {
public:
    ControlServerApp(boost::asio::io_context& io_context, ControlServerConfig config);

    void start();
    void stop();

private:
    boost::asio::io_context& io_context_;
    ControlServerConfig config_;
    CubeRegistry cube_registry_;
    FleetOrchestrator fleet_orchestrator_;
    RelayManager relay_manager_;
    WsServer ws_server_;
    CommandGateway command_gateway_;
    std::unique_ptr<boost::asio::steady_timer> fleet_timer_;

    void schedule_fleet_tick();
};

int run(const std::string& config_path);

}  // namespace toio::control
