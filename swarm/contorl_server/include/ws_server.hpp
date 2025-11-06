#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

namespace toio::control {

class WsServer {
public:
    using SessionId = std::uint64_t;
    using MessageHandler = std::function<void(const nlohmann::json&, SessionId)>;
    using SessionEventHandler = std::function<void(SessionId)>;

    explicit WsServer(boost::asio::io_context& io_context, std::string target_path = "/ws/ui");

    void start(const std::string& host, std::uint16_t port);
    void stop();

    void set_message_handler(MessageHandler handler);
    void set_open_handler(SessionEventHandler handler);
    void set_close_handler(SessionEventHandler handler);

    void broadcast(const nlohmann::json& message);
    void send(SessionId session_id, const nlohmann::json& message);

private:
    class WsSession : public std::enable_shared_from_this<WsSession> {
    public:
        WsSession(boost::asio::ip::tcp::socket socket, WsServer& server, SessionId id, boost::asio::io_context& io_context);

        void start();
        void enqueue(std::string message);
        SessionId id() const noexcept { return id_; }
        std::string remote_endpoint() const;

    private:
        void on_accept(boost::beast::error_code ec);
        void do_read();
        void on_read(boost::beast::error_code ec, std::size_t bytes);
        void do_write();
        void on_write(boost::beast::error_code ec, std::size_t bytes);
        void close_with_error(boost::beast::error_code ec);

        boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;
        boost::asio::strand<boost::asio::io_context::executor_type> strand_;
        boost::beast::flat_buffer buffer_;
        WsServer& server_;
        SessionId id_;
        std::deque<std::string> send_queue_;
        bool write_in_progress_{false};
    };

    void do_accept();
    void handle_session_message(SessionId session_id, std::string message);
    void handle_session_ready(SessionId session_id);
    void handle_session_closed(SessionId session_id);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::string host_;
    std::uint16_t port_{0};
    std::string target_path_;
    std::atomic_uint64_t next_session_id_{1};
    std::unordered_map<SessionId, std::shared_ptr<WsSession>> sessions_;
    MessageHandler message_handler_;
    SessionEventHandler open_handler_;
    SessionEventHandler close_handler_;
};

}  // namespace toio::control
