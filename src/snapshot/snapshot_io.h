#pragma once

#include "peer/peer_registry.h"

#include <cstdint>
#include <cstddef>
#include <string>

struct SnapshotObservation {
    std::string prefix;
    PeerId peer_id;
    uint32_t origin_asn;
    std::uint64_t timestamp;
};

struct SnapshotStats {
    std::size_t peers = 0;
    std::size_t observations = 0;
};

class RoutingState;
class PeerRegistry;

namespace SnapshotIO {

SnapshotStats save_snapshot(const std::string& path,
                            const PeerRegistry& registry,
                            const RoutingState& state);
SnapshotStats load_snapshot(const std::string& path,
                            PeerRegistry& registry,
                            RoutingState& state);

}  // namespace SnapshotIO
