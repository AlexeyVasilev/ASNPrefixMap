#include "prefix.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

uint32_t apply_mask_v4(uint32_t network, uint8_t length) {
    if (length == 0) {
        return 0;
    }

    const uint32_t mask = 0xFFFFFFFFu << (32 - length);
    return network & mask;
}

std::array<uint8_t, 16> apply_mask_v6(std::array<uint8_t, 16> network, uint8_t length) {
    uint8_t bits_left = length;
    for (std::size_t index = 0; index < network.size(); ++index) {
        if (bits_left >= 8) {
            bits_left -= 8;
            continue;
        }

        if (bits_left == 0) {
            network[index] = 0;
            continue;
        }

        const uint8_t mask = static_cast<uint8_t>(0xFFu << (8 - bits_left));
        network[index] &= mask;
        bits_left = 0;
    }

    return network;
}

std::pair<std::string, uint8_t> split_cidr(const std::string& cidr_text) {
    const std::size_t slash = cidr_text.find('/');
    if (slash == std::string::npos) {
        throw std::runtime_error("Prefix is missing CIDR length: " + cidr_text);
    }

    const std::string address = cidr_text.substr(0, slash);
    const std::string length_text = cidr_text.substr(slash + 1);
    const unsigned long length_value = std::stoul(length_text);
    if (length_value > 255) {
        throw std::runtime_error("Invalid CIDR length: " + cidr_text);
    }

    return {address, static_cast<uint8_t>(length_value)};
}

}  // namespace

std::size_t PrefixV4Hash::operator()(const PrefixV4& prefix) const noexcept {
    return (static_cast<std::size_t>(prefix.network) << 8) ^ prefix.length;
}

std::size_t PrefixV6Hash::operator()(const PrefixV6& prefix) const noexcept {
    std::size_t hash = prefix.length;
    for (uint8_t byte : prefix.network) {
        hash = (hash * 131u) ^ byte;
    }
    return hash;
}

BinaryPrefix parse_prefix(const std::string& cidr_text) {
    try {
        return parse_prefix_v4(cidr_text);
    } catch (const std::exception&) {
    }

    return parse_prefix_v6(cidr_text);
}

PrefixV4 parse_prefix_v4(const std::string& cidr_text) {
    const auto [address, length] = split_cidr(cidr_text);
    if (length > 32) {
        throw std::runtime_error("IPv4 prefix length out of range: " + cidr_text);
    }

    in_addr addr{};
    if (InetPtonA(AF_INET, address.c_str(), &addr) != 1) {
        throw std::runtime_error("Invalid IPv4 prefix: " + cidr_text);
    }

    const uint32_t host_order = ntohl(addr.S_un.S_addr);
    return PrefixV4{apply_mask_v4(host_order, length), length};
}

PrefixV6 parse_prefix_v6(const std::string& cidr_text) {
    const auto [address, length] = split_cidr(cidr_text);
    if (length > 128) {
        throw std::runtime_error("IPv6 prefix length out of range: " + cidr_text);
    }

    in6_addr addr6{};
    if (InetPtonA(AF_INET6, address.c_str(), &addr6) != 1) {
        throw std::runtime_error("Invalid IPv6 prefix: " + cidr_text);
    }

    std::array<uint8_t, 16> bytes{};
    std::memcpy(bytes.data(), &addr6, bytes.size());
    return PrefixV6{apply_mask_v6(bytes, length), length};
}

std::string to_string(const PrefixV4& prefix) {
    in_addr addr{};
    addr.S_un.S_addr = htonl(prefix.network);

    char buffer[INET_ADDRSTRLEN] = {};
    if (InetNtopA(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        throw std::runtime_error("Failed to stringify IPv4 prefix");
    }

    return std::string(buffer) + "/" + std::to_string(prefix.length);
}

std::string to_string(const PrefixV6& prefix) {
    in6_addr addr6{};
    std::memcpy(&addr6, prefix.network.data(), prefix.network.size());

    char buffer[INET6_ADDRSTRLEN] = {};
    if (InetNtopA(AF_INET6, &addr6, buffer, sizeof(buffer)) == nullptr) {
        throw std::runtime_error("Failed to stringify IPv6 prefix");
    }

    return std::string(buffer) + "/" + std::to_string(prefix.length);
}

std::string to_string(const BinaryPrefix& prefix) {
    return std::visit([](const auto& value) { return to_string(value); }, prefix);
}

PrefixFamily family_of(const BinaryPrefix& prefix) {
    return std::holds_alternative<PrefixV4>(prefix) ? PrefixFamily::V4 : PrefixFamily::V6;
}

const char* family_to_string(PrefixFamily family) {
    return family == PrefixFamily::V4 ? "v4" : "v6";
}

PrefixFamily parse_family(const std::string& family_text) {
    if (family_text == "v4") {
        return PrefixFamily::V4;
    }
    if (family_text == "v6") {
        return PrefixFamily::V6;
    }

    throw std::runtime_error("Unknown prefix family: " + family_text);
}
