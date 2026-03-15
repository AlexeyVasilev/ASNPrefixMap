#pragma once

#include <cstdint>
#include <string>

// Normalized event types used by parsers and future data sources.
enum class EventType {
    Announce,
    Withdraw
};

struct BgpEvent {
    EventType type;
    std::string peer;
    std::string prefix;
    uint32_t asn;
    std::uint64_t timestamp;
};
