#include "ris_live_websocket_source.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

#include <iostream>
#include <stdexcept>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

RisLiveWebSocketSource::RisLiveWebSocketSource(const std::string& host,
                                               const std::string& port,
                                               const std::string& target)
    : host_(host),
      port_(port),
      target_(target),
      ssl_context_(ssl::context::tls_client),
      connected_(false) {

    // Temporary: certificate verification is disabled because cross-platform
    // trust store loading is not implemented yet.
    // Acceptable for public RIS Live data ingestion, but not for security-sensitive use.
    ssl_context_.set_verify_mode(ssl::verify_none);

    connect();
    send_subscribe();
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
    if (!connected_ || !websocket_) {
        return false;
    }

    try {
        buffer_.consume(buffer_.size());
        websocket_->read(buffer_);
        json = beast::buffers_to_string(buffer_.data());
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[ris_live_ws] read error: " << ex.what() << '\n';
        connected_ = false;
        return false;
    }
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
