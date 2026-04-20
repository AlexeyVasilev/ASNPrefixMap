#pragma once

#include <boost/asio/error.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/system/system_error.hpp>

#include <cctype>
#include <sstream>
#include <string>

inline bool is_ascii_error_text(const std::string& text) {
    for (unsigned char ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            continue;
        }
        if (ch < 32 || ch > 126) {
            return false;
        }
    }
    return true;
}

inline std::string describe_error_code(const boost::system::error_code& ec) {
    namespace net = boost::asio;
    namespace ssl = boost::asio::ssl;

    if (ec == ssl::error::stream_truncated) {
        return "stream truncated";
    }
    if (ec == net::error::connection_reset) {
        return "connection reset by peer";
    }
    if (ec == net::error::connection_aborted) {
        return "connection aborted";
    }
    if (ec == net::error::connection_refused) {
        return "connection refused";
    }
    if (ec == net::error::timed_out) {
        return "timed out";
    }
    if (ec == net::error::eof) {
        return "end of stream";
    }
    if (ec == net::error::operation_aborted) {
        return "operation aborted";
    }
    if (ec == net::error::network_down) {
        return "network down";
    }
    if (ec == net::error::network_reset) {
        return "network reset";
    }
    if (ec == net::error::network_unreachable) {
        return "network unreachable";
    }
    if (ec == net::error::host_unreachable) {
        return "host unreachable";
    }

    const std::string raw_message = ec.message();
    if (!raw_message.empty() && is_ascii_error_text(raw_message)) {
        return raw_message;
    }

    return "system error";
}

inline std::string format_exception_message(const std::exception& ex) {
    if (const auto* system_ex = dynamic_cast<const boost::system::system_error*>(&ex)) {
        const auto& ec = system_ex->code();
        std::ostringstream out;
        out << describe_error_code(ec)
            << " [" << ec.category().name() << ':' << ec.value() << ']';
        return out.str();
    }

    return ex.what();
}
