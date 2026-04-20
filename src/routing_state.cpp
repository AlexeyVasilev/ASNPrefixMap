#include "routing_state.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace {

bool is_better_selection(uint32_t candidate_asn,
                         uint32_t candidate_count,
                         uint32_t current_asn,
                         uint32_t current_count) {
    return candidate_count > current_count ||
           (candidate_count == current_count && candidate_asn < current_asn);
}

uint32_t increment_asn_count(PrefixRecord& record, uint32_t asn) {
    return ++record.asn_counts[asn];
}

uint32_t decrement_asn_count(PrefixRecord& record, uint32_t asn) {
    const auto it = record.asn_counts.find(asn);
    if (it == record.asn_counts.end()) {
        return 0;
    }

    if (it->second <= 1) {
        record.asn_counts.erase(it);
        return 0;
    }

    --it->second;
    return it->second;
}

void recompute_selected_from_counts(PrefixRecord& record) {
    // When the previously selected ASN may no longer be valid, rescan the compact ASN
    // counters rather than rebuilding counts from every observation again.
    record.selected_asn = 0;
    record.selected_count = 0;

    for (const auto& [asn, count] : record.asn_counts) {
        if (is_better_selection(asn, count, record.selected_asn, record.selected_count)) {
            record.selected_asn = asn;
            record.selected_count = count;
        }
    }
}

template <typename Prefix, typename PrefixHash>
void sync_aggregated(std::unordered_map<Prefix, uint32_t, PrefixHash>& aggregated,
                     const Prefix& prefix,
                     const PrefixRecord& record) {
    if (record.selected_count == 0) {
        aggregated.erase(prefix);
        return;
    }

    aggregated[prefix] = record.selected_asn;
}

template <typename Prefix, typename PrefixHash>
void announce_impl(std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                   std::unordered_map<Prefix, uint32_t, PrefixHash>& aggregated,
                   const Prefix& prefix,
                   PeerId peer_id,
                   uint32_t origin_asn,
                   std::uint64_t timestamp) {
    PrefixRecord& record = by_prefix[prefix];

    for (std::size_t index = 0; index < record.observations.size(); ++index) {
        Observation& observation = record.observations[index];
        if (observation.peer_id != peer_id) {
            continue;
        }

        if (observation.origin_asn == origin_asn) {
            record.timestamps[index] = timestamp;
            return;
        }

        const uint32_t old_asn = observation.origin_asn;
        const bool selected_may_change = (old_asn == record.selected_asn);

        decrement_asn_count(record, old_asn);
        const uint32_t new_count = increment_asn_count(record, origin_asn);

        observation.origin_asn = origin_asn;
        record.timestamps[index] = timestamp;

        if (selected_may_change) {
            recompute_selected_from_counts(record);
        } else if (is_better_selection(origin_asn, new_count, record.selected_asn, record.selected_count)) {
            record.selected_asn = origin_asn;
            record.selected_count = new_count;
        }

        sync_aggregated(aggregated, prefix, record);
        return;
    }

    record.observations.push_back(Observation{peer_id, origin_asn});
    record.timestamps.push_back(timestamp);

    const uint32_t new_count = increment_asn_count(record, origin_asn);
    if (record.selected_count == 0 ||
        is_better_selection(origin_asn, new_count, record.selected_asn, record.selected_count)) {
        record.selected_asn = origin_asn;
        record.selected_count = new_count;
    }

    sync_aggregated(aggregated, prefix, record);
}

template <typename Prefix, typename PrefixHash>
void withdraw_impl(std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                   std::unordered_map<Prefix, uint32_t, PrefixHash>& aggregated,
                   const Prefix& prefix,
                   PeerId peer_id) {
    const auto it = by_prefix.find(prefix);
    if (it == by_prefix.end()) {
        return;
    }

    PrefixRecord& record = it->second;
    for (std::size_t index = 0; index < record.observations.size(); ++index) {
        const Observation removed = record.observations[index];
        if (removed.peer_id != peer_id) {
            continue;
        }

        const bool selected_may_change = (removed.origin_asn == record.selected_asn);
        decrement_asn_count(record, removed.origin_asn);

        // Packed removal avoids shifting the tail of the vector on every withdraw.
        const std::size_t last_index = record.observations.size() - 1;
        if (index != last_index) {
            record.observations[index] = record.observations[last_index];
            record.timestamps[index] = record.timestamps[last_index];
        }
        record.observations.pop_back();
        record.timestamps.pop_back();

        if (record.observations.empty()) {
            by_prefix.erase(it);
            aggregated.erase(prefix);
            return;
        }

        if (selected_may_change) {
            recompute_selected_from_counts(record);
        }

        sync_aggregated(aggregated, prefix, record);
        return;
    }
}

template <typename Prefix, typename PrefixHash>
void append_stored_observations(const std::unordered_map<Prefix, PrefixRecord, PrefixHash>& by_prefix,
                                std::vector<StoredObservation>& result) {
    for (const auto& [prefix, record] : by_prefix) {
        for (std::size_t index = 0; index < record.observations.size(); ++index) {
            result.push_back(StoredObservation{prefix, record.observations[index], record.timestamps[index]});
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
    announce_impl(by_prefix_v4_, aggregated_v4_, prefix, peer_id, origin_asn, timestamp);
}

void RoutingState::announce(PeerId peer_id,
                            const PrefixV6& prefix,
                            uint32_t origin_asn,
                            std::uint64_t timestamp) {
    announce_impl(by_prefix_v6_, aggregated_v6_, prefix, peer_id, origin_asn, timestamp);
}

void RoutingState::withdraw(PeerId peer_id,
                            const PrefixV4& prefix) {
    withdraw_impl(by_prefix_v4_, aggregated_v4_, prefix, peer_id);
}

void RoutingState::withdraw(PeerId peer_id,
                            const PrefixV6& prefix) {
    withdraw_impl(by_prefix_v6_, aggregated_v6_, prefix, peer_id);
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
    announce_impl(by_prefix_v4_, aggregated_v4_, prefix, peer_id, origin_asn, timestamp);
}

void RoutingState::restore_observation(PeerId peer_id,
                                       const PrefixV6& prefix,
                                       uint32_t origin_asn,
                                       std::uint64_t timestamp) {
    announce_impl(by_prefix_v6_, aggregated_v6_, prefix, peer_id, origin_asn, timestamp);
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
    PrefixRecord& record = by_prefix_v4_.at(prefix);
    recompute_selected_from_counts(record);
    sync_aggregated(aggregated_v4_, prefix, record);
}

void RoutingState::recompute_prefix_origin(const PrefixV6& prefix) {
    PrefixRecord& record = by_prefix_v6_.at(prefix);
    recompute_selected_from_counts(record);
    sync_aggregated(aggregated_v6_, prefix, record);
}
