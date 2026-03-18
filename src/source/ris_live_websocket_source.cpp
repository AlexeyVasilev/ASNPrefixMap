#include "ris_live_websocket_source.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

RisLiveWebSocketSource::RisLiveWebSocketSource(const std::string& host,
                                               const std::string& port,
                                               const std::string& target,
                                               bool reconnect_enabled,
                                               std::size_t reconnect_initial_delay_ms,
                                               std::size_t reconnect_max_delay_ms,
                                               std::size_t reconnect_max_attempts,
                                               std::function<bool()> shutdown_requested)
    : host_(host),
      port_(port),
      target_(target),
      reconnect_enabled_(reconnect_enabled),
      reconnect_initial_delay_ms_(reconnect_initial_delay_ms),
      reconnect_max_delay_ms_(std::max(reconnect_initial_delay_ms, reconnect_max_delay_ms)),
      reconnect_max_attempts_(reconnect_max_attempts),
      next_reconnect_delay_ms_(reconnect_initial_delay_ms),
      delay_before_reconnect_(false),
      shutdown_requested_(std::move(shutdown_requested)),
      ssl_context_(ssl::context::tls_client),
      connected_(false) {
    // Temporary: certificate verification is disabled because cross-platform
    // trust store loading is not implemented yet.
    // Acceptable for public RIS Live data ingestion, but not for security-sensitive use.
    ssl_context_.set_verify_mode(ssl::verify_none);
}

RisLiveWebSocketSource::~RisLiveWebSocketSource() {
    if (!connected_ || !websocket_) {
        return;
    }

    try {
        websocket_->close(websocket::close_code::normal);
        std::cerr << "[ris_live_ws] disconnect\n";
    } catch (const std::exception& ex) {
        std::cerr << "[ris_live_ws] disconnect error: " << ex.what() << '\n';
    }
}

bool RisLiveWebSocketSource::next_message(std::string& json) {
    while (!shutdown_requested()) {
        if (!ensure_connected()) {
            return false;
        }

        try {
            buffer_.consume(buffer_.size());
            websocket_->read(buffer_);
            json = beast::buffers_to_string(buffer_.data());
            return true;
        } catch (const std::exception& ex) {
            // Transient network or TLS errors should not terminate a long-running ingest.
            // Reconnect is best-effort and some live messages may be missed during the outage.
            std::cerr << "[ris_live_ws] read error: " << ex.what() << '\n';
            reset_connection();
        }
    }

    return false;
}

bool RisLiveWebSocketSource::ensure_connected() {
    if (connected_ && websocket_) {
        return true;
    }

    if (shutdown_requested()) {
        return false;
    }

    if (!reconnect_enabled_) {
        try {
            connect();
            send_subscribe();
            next_reconnect_delay_ms_ = reconnect_initial_delay_ms_;
            delay_before_reconnect_ = false;
            return true;
        } catch (const std::exception& ex) {
            std::cerr << "[ris_live_ws] connect error: " << ex.what() << '\n';
            reset_connection();
            return false;
        }
    }

    return reconnect_with_backoff();
}

bool RisLiveWebSocketSource::reconnect_with_backoff() {
    std::size_t attempt = 0;
    std::size_t delay_ms = next_reconnect_delay_ms_;
    bool delay_before_attempt = delay_before_reconnect_;

    while (!shutdown_requested()) {
        ++attempt;

        if (delay_before_attempt) {
            std::cerr << "[ris_live_ws] reconnect attempt " << attempt
                      << " in " << delay_ms << " ms\n";
            if (!sleep_with_stop(delay_ms)) {
                return false;
            }
        }

        try {
            connect();
            send_subscribe();
            next_reconnect_delay_ms_ = reconnect_initial_delay_ms_;
            if (delay_before_attempt || attempt > 1) {
                std::cerr << "[ris_live_ws] reconnect successful\n";
            }
            delay_before_reconnect_ = false;
            return true;
        } catch (const std::exception& ex) {
            reset_connection();
            if (reconnect_max_attempts_ != 0 && attempt >= reconnect_max_attempts_) {
                std::cerr << "[ris_live_ws] reconnect failure: " << ex.what() << "\n";
                return false;
            }

            delay_before_attempt = true;
            const std::size_t next_delay_ms = std::min(delay_ms * 2, reconnect_max_delay_ms_);
            std::cerr << "[ris_live_ws] reconnect failure: " << ex.what()
                      << "; next delay " << next_delay_ms << " ms\n";
            delay_ms = next_delay_ms;
            next_reconnect_delay_ms_ = delay_ms;
        }
    }

    return false;
}

void RisLiveWebSocketSource::connect() {
    std::cerr << "[ris_live_ws] connect " << host_ << ':' << port_ << target_ << '\n';

    tcp::resolver resolver(io_context_);
    const auto results = resolver.resolve(host_, port_);

    websocket_ = std::make_unique<WebSocketStream>(io_context_, ssl_context_);

    if (!SSL_set_tlsext_host_name(websocket_->next_layer().native_handle(), host_.c_str())) {
        throw std::runtime_error("Failed to set TLS SNI hostname");
    }

    beast::get_lowest_layer(*websocket_).connect(results);

    std::cerr << "[ris_live_ws] TLS handshake\n";
    websocket_->next_layer().handshake(ssl::stream_base::client);

    std::cerr << "[ris_live_ws] websocket handshake\n";
    websocket_->set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) + " ASNPrefixMap");
        }));
    websocket_->handshake(host_, target_);

    connected_ = true;
}

void RisLiveWebSocketSource::send_subscribe() {
    if (!connected_ || !websocket_) {
        throw std::runtime_error("WebSocket is not connected");
    }

    const std::string request = "{\"type\":\"ris_subscribe\",\"data\":{\"type\":\"UPDATE\"}}";
    websocket_->write(net::buffer(request));
    std::cerr << "[ris_live_ws] subscribe sent\n";
}

void RisLiveWebSocketSource::reset_connection() {
    connected_ = false;
    delay_before_reconnect_ = true;
    websocket_.reset();
    buffer_.consume(buffer_.size());
    io_context_.restart();
}

bool RisLiveWebSocketSource::shutdown_requested() const {
    return shutdown_requested_ && shutdown_requested_();
}

bool RisLiveWebSocketSource::sleep_with_stop(std::size_t delay_ms) const {
    constexpr std::size_t kSleepChunkMs = 100;
    std::size_t remaining = delay_ms;

    while (remaining > 0) {
        if (shutdown_requested()) {
            return false;
        }

        const std::size_t chunk = std::min(remaining, kSleepChunkMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
        remaining -= chunk;
    }

    return !shutdown_requested();
}

