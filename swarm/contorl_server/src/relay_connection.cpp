#include "relay_connection.hpp"

#include <boost/beast/core/buffers_to_string.hpp>
#include <stdexcept>
#include <string>

#include "util/logging.hpp"

namespace toio::control {

using boost::asio::ip::tcp;

RelayConnection::RelayConnection(boost::asio::io_context& io_context, Options options)
    : io_context_(io_context),
      strand_(boost::asio::make_strand(io_context)),
      resolver_(strand_),
      websocket_(strand_),
      reconnect_timer_(strand_),
      options_(std::move(options)) {}

void RelayConnection::set_message_handler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

void RelayConnection::set_status_handler(StatusHandler handler) {
    status_handler_ = std::move(handler);
}

void RelayConnection::start() {
    stopping_ = false;
    boost::asio::dispatch(strand_, [self = shared_from_this()]() {
        if (self->state_ == RelayConnectionState::Connecting || self->state_ == RelayConnectionState::Connected) {
            return;
        }
        self->parsed_uri_ = parse_uri(self->options_.uri);
        self->do_connect();
    });
}

void RelayConnection::stop() {
    boost::asio::dispatch(strand_, [self = shared_from_this()]() {
        self->stopping_ = true;
        self->reconnect_timer_.cancel();
        if (self->websocket_.is_open()) {
            self->websocket_.async_close(boost::beast::websocket::close_code::normal,
                                         [self](boost::beast::error_code) {});
        } else {
            boost::beast::error_code ec;
            self->websocket_.next_layer().close(ec);
        }
        self->state_ = RelayConnectionState::Stopped;
        self->outbound_queue_.clear();
    });
}

void RelayConnection::send(const nlohmann::json& message) {
    auto payload = message.dump();
    boost::asio::post(strand_, [self = shared_from_this(), payload = std::move(payload)]() mutable {
        if (self->state_ != RelayConnectionState::Connected) {
            return;
        }
        self->outbound_queue_.push_back(std::move(payload));
        if (!self->write_in_progress_) {
            self->write_in_progress_ = true;
            self->do_send();
        }
    });
}

void RelayConnection::do_connect() {
    state_ = RelayConnectionState::Connecting;
    notify_status(state_, "resolving");
    resolver_.async_resolve(
        parsed_uri_.host, parsed_uri_.port,
        boost::asio::bind_executor(
            strand_, [self = shared_from_this()](boost::beast::error_code ec, tcp::resolver::results_type results) {
                self->handle_resolve(ec, std::move(results));
            }));
}

void RelayConnection::handle_resolve(boost::beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        fail("resolve", ec);
        return;
    }

    boost::asio::async_connect(
        websocket_.next_layer(), results,
        boost::asio::bind_executor(strand_,
                                   [self = shared_from_this()](boost::beast::error_code connect_ec, const tcp::endpoint&) {
                                       self->handle_connect(connect_ec);
                                   }));
}

void RelayConnection::handle_connect(boost::beast::error_code ec) {
    if (ec) {
        fail("connect", ec);
        return;
    }

    websocket_.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
    websocket_.read_message_max(1 << 20);
    websocket_.async_handshake(
        parsed_uri_.host + ":" + parsed_uri_.port, parsed_uri_.target,
        boost::asio::bind_executor(strand_, [self = shared_from_this()](boost::beast::error_code handshake_ec) {
            self->handle_handshake(handshake_ec);
        }));
}

void RelayConnection::handle_handshake(boost::beast::error_code ec) {
    if (ec) {
        fail("handshake", ec);
        return;
    }

    state_ = RelayConnectionState::Connected;
    notify_status(state_, "connected");
    do_read();
    if (!outbound_queue_.empty()) {
        write_in_progress_ = true;
        do_send();
    }
}

void RelayConnection::do_read() {
    websocket_.async_read(
        read_buffer_, boost::asio::bind_executor(
                          strand_, [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes) {
                              self->on_read(ec, bytes);
                          }));
}

void RelayConnection::on_read(boost::beast::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) {
        if (ec == boost::beast::websocket::error::closed) {
            notify_status(RelayConnectionState::Stopped, "closed by remote");
        } else {
            fail("read", ec);
        }
        return;
    }

    const auto data = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());

    try {
        auto json = nlohmann::json::parse(data);
        if (message_handler_) {
            message_handler_(json);
        }
    } catch (const std::exception& ex) {
        util::log::warn(std::string("Failed to parse relay JSON: ") + ex.what());
    }

    do_read();
}

void RelayConnection::do_send() {
    if (outbound_queue_.empty()) {
        write_in_progress_ = false;
        return;
    }

    websocket_.async_write(
        boost::asio::buffer(outbound_queue_.front()),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](boost::beast::error_code ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    self->fail("write", ec);
                    return;
                }
                self->outbound_queue_.pop_front();
                if (self->outbound_queue_.empty()) {
                    self->write_in_progress_ = false;
                } else {
                    self->do_send();
                }
            }));
}

void RelayConnection::fail(const std::string& where, boost::beast::error_code ec) {
    util::log::warn("RelayConnection(" + options_.relay_id + ") " + where + " failed: " + ec.message());
    boost::beast::error_code close_ec;
    websocket_.next_layer().close(close_ec);
    state_ = RelayConnectionState::Stopped;
    notify_status(state_, where + " error");
    if (!stopping_) {
        schedule_reconnect();
    }
}

void RelayConnection::schedule_reconnect() {
    reconnect_timer_.expires_after(options_.reconnect_delay);
    reconnect_timer_.async_wait(boost::asio::bind_executor(
        strand_, [self = shared_from_this()](boost::beast::error_code ec) {
            if (ec || self->stopping_) {
                return;
            }
            self->do_connect();
        }));
}

void RelayConnection::notify_status(RelayConnectionState state, const std::string& message) {
    if (status_handler_) {
        status_handler_(state, message);
    }
}

RelayConnection::ParsedUri RelayConnection::parse_uri(const std::string& uri) {
    ParsedUri parsed;
    const std::string ws_prefix = "ws://";
    const std::string wss_prefix = "wss://";
    std::string remainder;
    if (uri.rfind(ws_prefix, 0) == 0) {
        parsed.secure = false;
        remainder = uri.substr(ws_prefix.size());
    } else if (uri.rfind(wss_prefix, 0) == 0) {
        parsed.secure = true;
        remainder = uri.substr(wss_prefix.size());
    } else {
        throw std::invalid_argument("Relay URI must start with ws:// or wss://");
    }

    auto slash_pos = remainder.find('/');
    std::string host_port = slash_pos == std::string::npos ? remainder : remainder.substr(0, slash_pos);
    parsed.target = slash_pos == std::string::npos ? "/" : remainder.substr(slash_pos);

    auto colon_pos = host_port.find(':');
    if (colon_pos == std::string::npos) {
        parsed.host = host_port;
        parsed.port = parsed.secure ? "443" : "80";
    } else {
        parsed.host = host_port.substr(0, colon_pos);
        parsed.port = host_port.substr(colon_pos + 1);
    }

    if (parsed.secure) {
        throw std::invalid_argument("wss:// relays are not supported yet");
    }

    return parsed;
}

}  // namespace toio::control
