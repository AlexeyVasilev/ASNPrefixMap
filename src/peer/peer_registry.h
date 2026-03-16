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
    std::unordered_map<std::string, PeerId> by_key_;
    std::unordered_map<PeerId, PeerInfo> by_id_;
    PeerId next_peer_id_ = 1;
};
