#include "peer_registry.h"

#include <algorithm>
#include <stdexcept>

std::size_t PeerInfoHash::operator()(const PeerInfo& peer) const noexcept {
    const std::size_t host_hash = std::hash<std::string>{}(peer.host);
    const std::size_t ip_hash = std::hash<std::string>{}(peer.peer_ip);
    const std::size_t asn_hash = std::hash<uint32_t>{}(peer.peer_asn);
    return host_hash ^ (ip_hash << 1) ^ (asn_hash << 2);
}

PeerId PeerRegistry::get_or_add(const PeerInfo& peer) {
    const auto it = by_info_.find(peer);
    if (it != by_info_.end()) {
        return it->second;
    }

    const PeerId peer_id = next_peer_id_++;
    by_info_[peer] = peer_id;
    by_id_[peer_id] = peer;
    return peer_id;
}

void PeerRegistry::insert_with_id(PeerId peer_id, const PeerInfo& peer) {
    if (by_id_.find(peer_id) != by_id_.end()) {
        throw std::runtime_error("Duplicate peer_id in registry: " + std::to_string(peer_id));
    }
    if (by_info_.find(peer) != by_info_.end()) {
        throw std::runtime_error("Duplicate peer identity in registry: " + make_key(peer));
    }

    by_id_[peer_id] = peer;
    by_info_[peer] = peer_id;
    if (peer_id >= next_peer_id_) {
        next_peer_id_ = peer_id + 1;
    }
}

bool PeerRegistry::contains(PeerId peer_id) const {
    return by_id_.find(peer_id) != by_id_.end();
}

const PeerInfo& PeerRegistry::get(PeerId peer_id) const {
    const auto it = by_id_.find(peer_id);
    if (it == by_id_.end()) {
        throw std::runtime_error("Unknown peer_id in registry: " + std::to_string(peer_id));
    }

    return it->second;
}

void PeerRegistry::clear() {
    by_info_.clear();
    by_id_.clear();
    next_peer_id_ = 1;
}

std::vector<std::pair<PeerId, PeerInfo>> PeerRegistry::all_peers() const {
    std::vector<std::pair<PeerId, PeerInfo>> peers;
    peers.reserve(by_id_.size());

    for (const auto& [peer_id, peer] : by_id_) {
        peers.push_back({peer_id, peer});
    }

    std::sort(peers.begin(), peers.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return peers;
}

std::string PeerRegistry::make_key(const PeerInfo& peer) {
    return peer.host + "|" + peer.peer_ip + "|" + std::to_string(peer.peer_asn);
}

PeerInfo PeerRegistry::parse_key(const std::string& key) {
    const std::size_t first_sep = key.find('|');
    const std::size_t second_sep = key.find('|', first_sep == std::string::npos ? first_sep : first_sep + 1);

    if (first_sep == std::string::npos || second_sep == std::string::npos) {
        throw std::runtime_error("Malformed peer key: " + key);
    }

    PeerInfo peer;
    peer.host = key.substr(0, first_sep);
    peer.peer_ip = key.substr(first_sep + 1, second_sep - first_sep - 1);
    peer.peer_asn = static_cast<uint32_t>(std::stoul(key.substr(second_sep + 1)));
    return peer;
}
