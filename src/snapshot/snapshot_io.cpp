#include "snapshot_io.h"

#include "peer/peer_registry.h"
#include "routing_state.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <set>
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

[[noreturn]] void throw_parse_error(std::size_t line_number, const std::string& message) {
    throw std::runtime_error("Snapshot parse error at line " + std::to_string(line_number) + ": " + message);
}

}  // namespace

namespace SnapshotIO {

SnapshotStats save_snapshot(const std::string& path,
                            const PeerRegistry& registry,
                            const RoutingState& state) {
    const std::vector<StoredObservation> stored = state.stored_observations();

    std::set<PeerId> used_peer_ids;
    for (const auto& entry : stored) {
        used_peer_ids.insert(entry.observation.peer_id);
    }

    std::vector<std::pair<PeerId, PeerInfo>> peers;
    peers.reserve(used_peer_ids.size());
    for (PeerId peer_id : used_peer_ids) {
        if (!registry.contains(peer_id)) {
            throw std::runtime_error("Routing state references unknown peer_id " + std::to_string(peer_id));
        }
        peers.push_back({peer_id, registry.get(peer_id)});
    }

    std::vector<SnapshotObservation> observations;
    observations.reserve(stored.size());
    for (const auto& entry : stored) {
        observations.push_back(SnapshotObservation{
            entry.prefix,
            entry.observation.peer_id,
            entry.observation.origin_asn,
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

    // Runtime state now uses PeerId, so snapshot can reuse the runtime registry directly
    // instead of rebuilding peer identities from duplicated peer strings.
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

SnapshotStats load_snapshot(const std::string& path,
                            PeerRegistry& registry,
                            RoutingState& state) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open snapshot input: " + path);
    }

    std::unordered_map<PeerId, PeerInfo> peers;
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

            const PeerId peer_id = static_cast<PeerId>(std::stoul(fields[0]));
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
            observation.peer_id = static_cast<PeerId>(std::stoul(fields[1]));
            observation.origin_asn = static_cast<uint32_t>(std::stoul(fields[2]));
            observation.timestamp = std::stoull(fields[3]);
            observations.push_back({observation.prefix, observation});
        }
    }

    if (!saw_any_content) {
        registry.clear();
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

    registry.clear();
    state.clear();

    for (const auto& [peer_id, peer] : peers) {
        registry.insert_with_id(peer_id, peer);
    }

    for (const auto& [prefix, observation] : observations) {
        static_cast<void>(prefix);
        if (!registry.contains(observation.peer_id)) {
            throw std::runtime_error("Snapshot parse error: observation references unknown peer_id " +
                                     std::to_string(observation.peer_id));
        }

        state.restore_observation(
            observation.peer_id,
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
