#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>

struct PrefixV4 {
    uint32_t network;
    uint8_t length;

    bool operator==(const PrefixV4& other) const = default;
};

struct PrefixV6 {
    std::array<uint8_t, 16> network;
    uint8_t length;

    bool operator==(const PrefixV6& other) const = default;
};

using BinaryPrefix = std::variant<PrefixV4, PrefixV6>;

enum class PrefixFamily {
    V4,
    V6
};

struct PrefixV4Hash {
    std::size_t operator()(const PrefixV4& prefix) const noexcept;
};

struct PrefixV6Hash {
    std::size_t operator()(const PrefixV6& prefix) const noexcept;
};

BinaryPrefix parse_prefix(const std::string& cidr_text);
PrefixV4 parse_prefix_v4(const std::string& cidr_text);
PrefixV6 parse_prefix_v6(const std::string& cidr_text);
std::string to_string(const PrefixV4& prefix);
std::string to_string(const PrefixV6& prefix);
std::string to_string(const BinaryPrefix& prefix);
PrefixFamily family_of(const BinaryPrefix& prefix);
const char* family_to_string(PrefixFamily family);
PrefixFamily parse_family(const std::string& family_text);
