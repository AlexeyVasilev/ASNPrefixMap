#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

struct PeerInfo {
    std::string host;
    std::string peer_ip;
    uint32_t peer_asn;
};

struct SnapshotObservation {
    std::string prefix;
    uint32_t peer_id;
    uint32_t origin_asn;
    std::uint64_t timestamp;
};

struct SnapshotStats {
    std::size_t peers = 0;
    std::size_t observations = 0;
};

class RoutingState;

namespace SnapshotIO {

SnapshotStats save_snapshot(const std::string& path, const RoutingState& state);
SnapshotStats load_snapshot(const std::string& path, RoutingState& state);

}  // namespace SnapshotIO
