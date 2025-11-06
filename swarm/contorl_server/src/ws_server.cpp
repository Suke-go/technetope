#include "ws_server.hpp"

#include <boost/beast/core/buffers_to_string.hpp>

#include "util/logging.hpp"

namespace toio::control {

using boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

WsServer::WsServer(boost::asio::io_context& io_context, std::string target_path)
    : io_context_(io_context),
      acceptor_(io_context),
      strand_(boost::asio::make_strand(io_context)),
      target_path_(std::move(target_path)) {}

void WsServer::start(const std::string& host, std::uint16_t port) {
    boost::asio::dispatch(strand_, [this, host, port]() {
        host_ = host;
        port_ = port;
        boost::system::error_code ec;
        auto address = boost::asio::ip::make_address(host, ec);
        if (ec) {
            util::log::error("WsServer invalid address: " + host + " - " + ec.message());
            return;
        }

        tcp::endpoint endpoint{address, port};
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            util::log::error("WsServer open failed: " + ec.message());
            return;
        }
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint, ec);
        if (ec) {
            util::log::error("WsServer bind failed: " + ec.message());
            return;
        }
        acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            util::log::error("WsServer listen failed: " + ec.message());
            return;
        }

        util::log::info("WsServer listening on " + host + ":" + std::to_string(port));
        do_accept();
    });
}

void WsServer::stop() {
    boost::asio::dispatch(strand_, [this]() {
        boost::system::error_code ec;
        acceptor_.close(ec);
        for (auto& [_, session] : sessions_) {
            (void)_;
            session->enqueue(R"({"type":"log","payload":{"level":"info","message":"server stopping"}})");
        }
        sessions_.clear();
    });
}

void WsServer::set_message_handler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

void WsServer::set_open_handler(SessionEventHandler handler) {
    open_handler_ = std::move(handler);
}

void WsServer::set_close_handler(SessionEventHandler handler) {
    close_handler_ = std::move(handler);
}

void WsServer::broadcast(const nlohmann::json& message) {
    const auto text = message.dump();
    boost::asio::dispatch(strand_, [this, text]() {
        for (auto& [_, session] : sessions_) {
            session->enqueue(text);
        }
    });
}

void WsServer::send(SessionId session_id, const nlohmann::json& message) {
    const auto text = message.dump();
    boost::asio::dispatch(strand_, [this, session_id, text]() {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->enqueue(text);
        }
    });
}

void WsServer::do_accept() {
    acceptor_.async_accept(boost::asio::bind_executor(
        strand_, [this](boost::system::error_code ec, tcp::socket socket) {
            if (ec) {
                if (acceptor_.is_open()) {
                    util::log::warn("WsServer accept error: " + ec.message());
                }
                return;
            }

            auto session_id = next_session_id_++;
            auto session = std::make_shared<WsSession>(std::move(socket), *this, session_id, io_context_);
            sessions_.emplace(session_id, session);
            session->start();

            if (acceptor_.is_open()) {
                do_accept();
            }
        }));
}

void WsServer::handle_session_message(SessionId session_id, std::string message) {
    boost::asio::dispatch(strand_, [this, session_id, message = std::move(message)]() mutable {
        if (!message_handler_) {
            return;
        }
        try {
            auto json = nlohmann::json::parse(message);
            message_handler_(json, session_id);
        } catch (const std::exception& ex) {
            util::log::warn("WsServer received invalid JSON: " + std::string(ex.what()));
        }
    });
}

void WsServer::handle_session_ready(SessionId session_id) {
    boost::asio::dispatch(strand_, [this, session_id]() {
        if (open_handler_) {
            open_handler_(session_id);
        }
    });
}

void WsServer::handle_session_closed(SessionId session_id) {
    boost::asio::dispatch(strand_, [this, session_id]() {
        sessions_.erase(session_id);
        if (close_handler_) {
            close_handler_(session_id);
        }
    });
}

WsServer::WsSession::WsSession(tcp::socket socket, WsServer& server, SessionId id, boost::asio::io_context& io_context)
    : ws_(std::move(socket)),
      strand_(boost::asio::make_strand(io_context)),
      server_(server),
      id_(id) {}

void WsServer::WsSession::start() {
    ws_.text(true);
    ws_.set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
    ws_.async_accept(boost::asio::bind_executor(
        strand_, [self = shared_from_this()](boost::beast::error_code ec) { self->on_accept(ec); }));
}

void WsServer::WsSession::enqueue(std::string message) {
    boost::asio::post(strand_, [self = shared_from_this(), message = std::move(message)]() mutable {
        self->send_queue_.push_back(std::move(message));
        if (!self->write_in_progress_) {
            self->write_in_progress_ = true;
            self->do_write();
        }
    });
}

std::string WsServer::WsSession::remote_endpoint() const {
    boost::system::error_code ec;
    const auto endpoint = ws_.next_layer().remote_endpoint(ec);
    if (ec) {
        return {};
    }
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

void WsServer::WsSession::on_accept(boost::beast::error_code ec) {
    if (ec) {
        close_with_error(ec);
        return;
    }
    server_.handle_session_ready(id_);
    do_read();
}

void WsServer::WsSession::do_read() {
    ws_.async_read(buffer_, boost::asio::bind_executor(
                              strand_, [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes) {
                                  self->on_read(ec, bytes);
                              }));
}

void WsServer::WsSession::on_read(boost::beast::error_code ec, std::size_t /*bytes*/) {
    if (ec) {
        close_with_error(ec);
        return;
    }
    const auto text = boost::beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    server_.handle_session_message(id_, text);
    do_read();
}

void WsServer::WsSession::do_write() {
    if (send_queue_.empty()) {
        write_in_progress_ = false;
        return;
    }
    ws_.async_write(
        boost::asio::buffer(send_queue_.front()),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes) { self->on_write(ec, bytes); }));
}

void WsServer::WsSession::on_write(boost::beast::error_code ec, std::size_t /*bytes*/) {
    if (ec) {
        close_with_error(ec);
        return;
    }
    send_queue_.pop_front();
    if (!send_queue_.empty()) {
        do_write();
    } else {
        write_in_progress_ = false;
    }
}

void WsServer::WsSession::close_with_error(boost::beast::error_code ec) {
    if (ec != boost::beast::websocket::error::closed) {
        util::log::warn("WebSocket session error: " + ec.message());
    }
    boost::system::error_code close_ec;
    ws_.next_layer().shutdown(tcp::socket::shutdown_both, close_ec);
    ws_.next_layer().close(close_ec);
    server_.handle_session_closed(id_);
}

}  // namespace toio::control
