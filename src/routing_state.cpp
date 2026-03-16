#include "routing_state.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace {

template <typename Prefix, typename PrefixHash>
void announce_impl(std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                   const Prefix& prefix,
                   PeerId peer_id,
                   uint32_t origin_asn,
                   std::uint64_t timestamp) {
    PrefixRecord& record = by_prefix[prefix];

    for (auto& observation : record.observations) {
        if (observation.peer_id == peer_id) {
            observation.origin_asn = origin_asn;
            observation.timestamp = timestamp;
            return;
        }
    }

    record.observations.push_back(Observation{peer_id, origin_asn, timestamp});
}

template <typename Prefix, typename PrefixHash>
bool withdraw_impl(std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                   const Prefix& prefix,
                   PeerId peer_id) {
    const auto it = by_prefix.find(prefix);
    if (it == by_prefix.end()) {
        return false;
    }

    PrefixRecord& record = it->second;
    record.observations.erase(
        std::remove_if(record.observations.begin(),
                       record.observations.end(),
                       [peer_id](const Observation& observation) {
                           return observation.peer_id == peer_id;
                       }),
        record.observations.end());

    if (record.observations.empty()) {
        by_prefix.erase(it);
        return true;
    }

    return false;
}

template <typename Prefix, typename PrefixHash>
uint32_t select_origin_asn(const std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                           const Prefix& prefix) {
    std::unordered_map<uint32_t, int> counts;

    for (const auto& observation : by_prefix.at(prefix).observations) {
        counts[observation.origin_asn]++;
    }

    uint32_t best_asn = 0;
    int best_count = -1;
    for (const auto& [asn, count] : counts) {
        if (count > best_count || (count == best_count && asn < best_asn)) {
            best_count = count;
            best_asn = asn;
        }
    }

    return best_asn;
}

template <typename Prefix, typename PrefixHash>
void append_stored_observations(const std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                                std::vector<StoredObservation>& result) {
    for (const auto& [prefix, record] : by_prefix) {
        for (const auto& observation : record.observations) {
            result.push_back(StoredObservation{prefix, observation});
        }
    }
}

template <typename Prefix, typename PrefixHash>
void append_exports(const std::unordered_map<Prefix, uint32_t, PrefixHash>& aggregated,
                    std::ofstream& prefix_stream,
                    std::unordered_map<uint32_t, std::vector<std::string>>& asn_map) {
    for (const auto& [prefix, asn] : aggregated) {
        const std::string prefix_text = to_string(prefix);
        prefix_stream << prefix_text << '\t' << asn << '\n';
        asn_map[asn].push_back(prefix_text);
    }
}

}  // namespace

void RoutingState::announce(PeerId peer_id,
                            const PrefixV4& prefix,
                            uint32_t origin_asn,
                            std::uint64_t timestamp) {
    announce_impl(by_prefix_v4_, prefix, peer_id, origin_asn, timestamp);
    recompute_prefix_origin(prefix);
}

void RoutingState::announce(PeerId peer_id,
                            const PrefixV6& prefix,
                            uint32_t origin_asn,
                            std::uint64_t timestamp) {
    announce_impl(by_prefix_v6_, prefix, peer_id, origin_asn, timestamp);
    recompute_prefix_origin(prefix);
}

void RoutingState::withdraw(PeerId peer_id,
                            const PrefixV4& prefix) {
    if (withdraw_impl(by_prefix_v4_, prefix, peer_id)) {
        aggregated_v4_.erase(prefix);
        return;
    }

    if (by_prefix_v4_.find(prefix) != by_prefix_v4_.end()) {
        recompute_prefix_origin(prefix);
    }
}

void RoutingState::withdraw(PeerId peer_id,
                            const PrefixV6& prefix) {
    if (withdraw_impl(by_prefix_v6_, prefix, peer_id)) {
        aggregated_v6_.erase(prefix);
        return;
    }

    if (by_prefix_v6_.find(prefix) != by_prefix_v6_.end()) {
        recompute_prefix_origin(prefix);
    }
}

void RoutingState::export_tables(const std::string& prefix_file,
                                 const std::string& asn_file) const {
    std::ofstream prefix_stream(prefix_file);
    std::ofstream asn_stream(asn_file);

    std::unordered_map<uint32_t, std::vector<std::string>> asn_map;
    append_exports(aggregated_v4_, prefix_stream, asn_map);
    append_exports(aggregated_v6_, prefix_stream, asn_map);

    for (auto& [asn, prefixes] : asn_map) {
        std::sort(prefixes.begin(), prefixes.end());
        asn_stream << asn;
        for (const auto& prefix : prefixes) {
            asn_stream << '\t' << prefix;
        }
        asn_stream << '\n';
    }
}

std::vector<StoredObservation> RoutingState::stored_observations() const {
    std::vector<StoredObservation> result;
    append_stored_observations(by_prefix_v4_, result);
    append_stored_observations(by_prefix_v6_, result);
    return result;
}

std::size_t RoutingState::active_prefixes_v4_count() const {
    return aggregated_v4_.size();
}

std::size_t RoutingState::active_prefixes_v6_count() const {
    return aggregated_v6_.size();
}

std::size_t RoutingState::active_prefixes_total_count() const {
    return aggregated_v4_.size() + aggregated_v6_.size();
}

void RoutingState::clear() {
    by_prefix_v4_.clear();
    by_prefix_v6_.clear();
    aggregated_v4_.clear();
    aggregated_v6_.clear();
}

void RoutingState::restore_observation(PeerId peer_id,
                                       const PrefixV4& prefix,
                                       uint32_t origin_asn,
                                       std::uint64_t timestamp) {
    by_prefix_v4_[prefix].observations.push_back(Observation{peer_id, origin_asn, timestamp});
}

void RoutingState::restore_observation(PeerId peer_id,
                                       const PrefixV6& prefix,
                                       uint32_t origin_asn,
                                       std::uint64_t timestamp) {
    by_prefix_v6_[prefix].observations.push_back(Observation{peer_id, origin_asn, timestamp});
}

void RoutingState::rebuild_aggregated() {
    aggregated_v4_.clear();
    aggregated_v6_.clear();

    for (const auto& [prefix, record] : by_prefix_v4_) {
        static_cast<void>(record);
        recompute_prefix_origin(prefix);
    }
    for (const auto& [prefix, record] : by_prefix_v6_) {
        static_cast<void>(record);
        recompute_prefix_origin(prefix);
    }
}

void RoutingState::recompute_prefix_origin(const PrefixV4& prefix) {
    aggregated_v4_[prefix] = select_origin_asn(by_prefix_v4_, prefix);
}

void RoutingState::recompute_prefix_origin(const PrefixV6& prefix) {
    aggregated_v6_[prefix] = select_origin_asn(by_prefix_v6_, prefix);
}
