#include "snapshot_io.h"

#include "routing_state.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class Section {
    None,
    Peers,
    Observations
};

std::vector<std::string> split_tab_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (true) {
        const std::size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }

        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

PeerInfo parse_peer_string(const std::string& peer) {
    const std::size_t first_sep = peer.find('|');
    const std::size_t second_sep = peer.find('|', first_sep == std::string::npos ? first_sep : first_sep + 1);

    if (first_sep == std::string::npos || second_sep == std::string::npos) {
        throw std::runtime_error("Malformed peer string in routing state: " + peer);
    }

    PeerInfo info;
    info.host = peer.substr(0, first_sep);
    info.peer_ip = peer.substr(first_sep + 1, second_sep - first_sep - 1);
    info.peer_asn = static_cast<uint32_t>(std::stoul(peer.substr(second_sep + 1)));
    return info;
}

std::string build_peer_string(const PeerInfo& peer) {
    return peer.host + "|" + peer.peer_ip + "|" + std::to_string(peer.peer_asn);
}

[[noreturn]] void throw_parse_error(std::size_t line_number, const std::string& message) {
    throw std::runtime_error("Snapshot parse error at line " + std::to_string(line_number) + ": " + message);
}

}  // namespace

namespace SnapshotIO {

SnapshotStats save_snapshot(const std::string& path, const RoutingState& state) {
    const std::vector<StoredObservation> stored = state.stored_observations();

    std::map<std::string, uint32_t> peer_ids;
    std::vector<std::pair<uint32_t, PeerInfo>> peers;
    uint32_t next_peer_id = 1;

    // Snapshot stores internal observations, not only exports, because withdraw correctness
    // after restart depends on the full per-peer state rather than the derived prefix->ASN table.
    for (const auto& entry : stored) {
        if (peer_ids.find(entry.peer) != peer_ids.end()) {
            continue;
        }

        const uint32_t peer_id = next_peer_id++;
        peer_ids.emplace(entry.peer, peer_id);
        peers.push_back({peer_id, parse_peer_string(entry.peer)});
    }

    std::sort(peers.begin(), peers.end(), [](const auto& left, const auto& right) {
        if (left.second.host != right.second.host) {
            return left.second.host < right.second.host;
        }
        if (left.second.peer_ip != right.second.peer_ip) {
            return left.second.peer_ip < right.second.peer_ip;
        }
        return left.second.peer_asn < right.second.peer_asn;
    });

    peer_ids.clear();
    for (std::size_t index = 0; index < peers.size(); ++index) {
        peers[index].first = static_cast<uint32_t>(index + 1);
        peer_ids[build_peer_string(peers[index].second)] = peers[index].first;
    }

    std::vector<SnapshotObservation> observations;
    observations.reserve(stored.size());
    for (const auto& entry : stored) {
        observations.push_back(SnapshotObservation{
            entry.prefix,
            peer_ids.at(entry.peer),
            entry.observation.asn,
            entry.observation.timestamp,
        });
    }

    std::sort(observations.begin(), observations.end(), [](const auto& left, const auto& right) {
        if (left.prefix != right.prefix) {
            return left.prefix < right.prefix;
        }
        if (left.peer_id != right.peer_id) {
            return left.peer_id < right.peer_id;
        }
        if (left.origin_asn != right.origin_asn) {
            return left.origin_asn < right.origin_asn;
        }
        return left.timestamp < right.timestamp;
    });

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open snapshot output: " + path);
    }

    output << "# ASNPrefixMap snapshot v1\n\n";
    output << "[peers]\n";

    // Peer registry is introduced in the snapshot early to reduce duplication on disk
    // and to prepare for a later runtime refactor to numeric PeerId storage.
    for (const auto& [peer_id, peer] : peers) {
        output << peer_id << '\t'
               << peer.host << '\t'
               << peer.peer_ip << '\t'
               << peer.peer_asn << '\n';
    }

    output << "\n[observations]\n";
    for (const auto& observation : observations) {
        output << observation.prefix << '\t'
               << observation.peer_id << '\t'
               << observation.origin_asn << '\t'
               << observation.timestamp << '\n';
    }

    return SnapshotStats{peers.size(), observations.size()};
}

SnapshotStats load_snapshot(const std::string& path, RoutingState& state) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open snapshot input: " + path);
    }

    std::unordered_map<uint32_t, PeerInfo> peers;
    std::vector<std::pair<std::string, SnapshotObservation>> observations;
    Section section = Section::None;
    bool saw_any_content = false;
    bool saw_header = false;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        saw_any_content = true;

        if (!saw_header) {
            if (trimmed != "# ASNPrefixMap snapshot v1") {
                throw_parse_error(line_number, "expected snapshot header '# ASNPrefixMap snapshot v1'");
            }
            saw_header = true;
            continue;
        }

        if (trimmed == "[peers]") {
            section = Section::Peers;
            continue;
        }
        if (trimmed == "[observations]") {
            section = Section::Observations;
            continue;
        }
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            throw_parse_error(line_number, "unknown section name: " + trimmed);
        }
        if (section == Section::None) {
            throw_parse_error(line_number, "data line found before any section");
        }

        const std::vector<std::string> fields = split_tab_fields(line);
        if (section == Section::Peers) {
            if (fields.size() != 4) {
                throw_parse_error(line_number, "peer line must have 4 tab-separated fields");
            }

            const uint32_t peer_id = static_cast<uint32_t>(std::stoul(fields[0]));
            if (peers.find(peer_id) != peers.end()) {
                throw_parse_error(line_number, "duplicate peer_id: " + fields[0]);
            }

            peers.emplace(peer_id, PeerInfo{fields[1], fields[2], static_cast<uint32_t>(std::stoul(fields[3]))});
        } else if (section == Section::Observations) {
            if (fields.size() != 4) {
                throw_parse_error(line_number, "observation line must have 4 tab-separated fields");
            }

            SnapshotObservation observation;
            observation.prefix = fields[0];
            observation.peer_id = static_cast<uint32_t>(std::stoul(fields[1]));
            observation.origin_asn = static_cast<uint32_t>(std::stoul(fields[2]));
            observation.timestamp = std::stoull(fields[3]);
            observations.push_back({observation.prefix, observation});
        }
    }

    // Empty snapshot file is tolerated and treated as empty state.
    if (!saw_any_content) {
        state.clear();
        return SnapshotStats{};
    }

    if (!saw_header) {
        throw std::runtime_error("Snapshot parse error: missing snapshot header");
    }

    std::sort(observations.begin(), observations.end(), [](const auto& left, const auto& right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        if (left.second.peer_id != right.second.peer_id) {
            return left.second.peer_id < right.second.peer_id;
        }
        if (left.second.origin_asn != right.second.origin_asn) {
            return left.second.origin_asn < right.second.origin_asn;
        }
        return left.second.timestamp < right.second.timestamp;
    });

    state.clear();
    for (const auto& [prefix, observation] : observations) {
        static_cast<void>(prefix);
        const auto peer_it = peers.find(observation.peer_id);
        if (peer_it == peers.end()) {
            throw std::runtime_error("Snapshot parse error: observation references unknown peer_id " +
                                     std::to_string(observation.peer_id));
        }

        state.restore_observation(
            build_peer_string(peer_it->second),
            observation.prefix,
            observation.origin_asn,
            observation.timestamp);
    }

    // Derived exports are recomputed from restored observations because the snapshot
    // should not serialize and trust redundant aggregated tables.
    state.rebuild_aggregated();
    return SnapshotStats{peers.size(), observations.size()};
}

}  // namespace SnapshotIO
