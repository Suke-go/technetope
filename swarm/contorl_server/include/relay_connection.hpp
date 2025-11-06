#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

namespace toio::control {

enum class RelayConnectionState {
    Stopped,
    Connecting,
    Connected
};

class RelayConnection : public std::enable_shared_from_this<RelayConnection> {
public:
    struct Options {
        std::string relay_id;
        std::string uri;
        std::chrono::milliseconds reconnect_delay{2000};
    };

    using MessageHandler = std::function<void(const nlohmann::json&)>;
    using StatusHandler = std::function<void(RelayConnectionState, const std::string& message)>;

    RelayConnection(boost::asio::io_context& io_context, Options options);

    void start();
    void stop();
    void send(const nlohmann::json& message);

    void set_message_handler(MessageHandler handler);
    void set_status_handler(StatusHandler handler);

    const std::string& relay_id() const noexcept { return options_.relay_id; }

private:
    struct ParsedUri {
        std::string host;
        std::string port;
        std::string target;
        bool secure{false};
    };

    void do_connect();
    void handle_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void handle_connect(boost::beast::error_code ec);
    void handle_handshake(boost::beast::error_code ec);
    void do_read();
    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
    void fail(const std::string& where, boost::beast::error_code ec);
    void schedule_reconnect();
    void notify_status(RelayConnectionState state, const std::string& message);
    void do_send();

    static ParsedUri parse_uri(const std::string& uri);

    boost::asio::io_context& io_context_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket_;
    boost::beast::flat_buffer read_buffer_;
    boost::asio::steady_timer reconnect_timer_;
    std::deque<std::string> outbound_queue_;
    bool stopping_{false};
    bool write_in_progress_{false};
    RelayConnectionState state_{RelayConnectionState::Stopped};
    Options options_;
    ParsedUri parsed_uri_;
    MessageHandler message_handler_;
    StatusHandler status_handler_;
};

}  // namespace toio::control
