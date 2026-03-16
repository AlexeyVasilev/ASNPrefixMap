#pragma once

#include "peer/peer_registry.h"
#include "prefix/prefix.h"

#include <cstdint>
#include <string>

// Normalized events still keep prefix text at the parser boundary.
// Runtime converts it to binary prefix types before inserting into RoutingState.
enum class EventType {
    Announce,
    Withdraw
};

struct BgpEvent {
    EventType type;
    PeerInfo peer;
    std::string prefix;
    uint32_t asn;
    std::uint64_t timestamp;
};
