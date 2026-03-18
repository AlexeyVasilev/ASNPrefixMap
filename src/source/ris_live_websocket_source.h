#pragma once

#include "bgp_source.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <functional>
#include <memory>
#include <string>

class RisLiveWebSocketSource : public BgpSource {
public:
    RisLiveWebSocketSource(const std::string& host,
                           const std::string& port,
                           const std::string& target,
                           bool reconnect_enabled,
                           std::size_t reconnect_initial_delay_ms,
                           std::size_t reconnect_max_delay_ms,
                           std::size_t reconnect_max_attempts,
                           std::function<bool()> shutdown_requested);
    ~RisLiveWebSocketSource() override;

    bool next_message(std::string& json) override;

private:
    using WebSocketStream = boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    bool ensure_connected();
    bool reconnect_with_backoff();
    void connect();
    void send_subscribe();
    void reset_connection();
    bool shutdown_requested() const;
    bool sleep_with_stop(std::size_t delay_ms) const;

    std::string host_;
    std::string port_;
    std::string target_;
    bool reconnect_enabled_;
    std::size_t reconnect_initial_delay_ms_;
    std::size_t reconnect_max_delay_ms_;
    std::size_t reconnect_max_attempts_;
    std::size_t next_reconnect_delay_ms_;
    bool delay_before_reconnect_;
    std::function<bool()> shutdown_requested_;
    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    std::unique_ptr<WebSocketStream> websocket_;
    boost::beast::flat_buffer buffer_;
    bool connected_;
};
