#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using PeerId = uint32_t;

struct PeerInfo {
    std::string host;
    std::string peer_ip;
    uint32_t peer_asn = 0;

    bool operator==(const PeerInfo& other) const = default;
};

struct PeerInfoHash {
    std::size_t operator()(const PeerInfo& peer) const noexcept;
};

class PeerRegistry {
public:
    PeerId get_or_add(const PeerInfo& peer);
    void insert_with_id(PeerId peer_id, const PeerInfo& peer);
    bool contains(PeerId peer_id) const;
    const PeerInfo& get(PeerId peer_id) const;
    void clear();
    std::vector<std::pair<PeerId, PeerInfo>> all_peers() const;

    static std::string make_key(const PeerInfo& peer);
    static PeerInfo parse_key(const std::string& key);

private:
    // Structured peer keys avoid building temporary composite strings on the hot ingest path.
    // This gives the obvious allocation win now without introducing a string pool yet.
    std::unordered_map<PeerInfo, PeerId, PeerInfoHash> by_info_;
    std::unordered_map<PeerId, PeerInfo> by_id_;
    PeerId next_peer_id_ = 1;
};
