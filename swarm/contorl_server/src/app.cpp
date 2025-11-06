#include "app.hpp"

#include <chrono>
#include <csignal>
#include <utility>

#include <boost/asio/signal_set.hpp>

#include "util/config_loader.hpp"
#include "util/logging.hpp"

namespace toio::control {

ControlServerApp::ControlServerApp(boost::asio::io_context& io_context, ControlServerConfig config)
    : io_context_(io_context),
      config_(std::move(config)),
      cube_registry_(),
      fleet_orchestrator_(cube_registry_),
      relay_manager_(io_context_, cube_registry_, config_),
      ws_server_(io_context_),
      command_gateway_(ws_server_, relay_manager_, cube_registry_, fleet_orchestrator_, config_.field) {}

void ControlServerApp::start() {
    ws_server_.set_open_handler([this](WsServer::SessionId session_id) { command_gateway_.handle_open(session_id); });
    ws_server_.set_close_handler([this](WsServer::SessionId session_id) { command_gateway_.handle_close(session_id); });
    ws_server_.set_message_handler(
        [this](const nlohmann::json& message, WsServer::SessionId session_id) { command_gateway_.handle_message(message, session_id); });

    relay_manager_.set_status_callback(
        [this](const RelayStatusEvent& event) { command_gateway_.publish_relay_status(event); });
    relay_manager_.set_cube_update_callback(
        [this](const std::vector<CubeRegistry::CubeState>& updates) { command_gateway_.publish_cube_updates(updates); });
    relay_manager_.set_log_callback(
        [this](const std::string& level, const std::string& message, const nlohmann::json& context) {
            command_gateway_.publish_log(level, message, context);
        });

    ws_server_.start(config_.ui.host, config_.ui.port);
    relay_manager_.start();

    fleet_timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
    schedule_fleet_tick();
}

void ControlServerApp::stop() {
    if (fleet_timer_) {
        fleet_timer_->cancel();
    }
    relay_manager_.stop();
    ws_server_.stop();
}

void ControlServerApp::schedule_fleet_tick() {
    if (!fleet_timer_) {
        return;
    }
    fleet_timer_->expires_after(std::chrono::seconds(1));
    fleet_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !fleet_timer_) {
            return;
        }
        command_gateway_.publish_fleet_state();
        schedule_fleet_tick();
    });
}

int run(const std::string& config_path) {
    try {
        auto config = load_config(config_path);
        boost::asio::io_context io_context;
        ControlServerApp app(io_context, config);
        app.start();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            util::log::info("Signal received, shutting down...");
            app.stop();
            io_context.stop();
        });

        io_context.run();
    } catch (const std::exception& ex) {
        util::log::error(std::string("Fatal error: ") + ex.what());
        return 1;
    }
    return 0;
}

}  // namespace toio::control
