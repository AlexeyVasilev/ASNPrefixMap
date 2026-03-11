#include "routing_state.h"

#include <fstream>
#include <unordered_map>

void RoutingState::announce(const std::string& peer,
                            const std::string& prefix,
                            uint32_t asn) {
    by_prefix_[prefix][peer] = Observation{asn};
    recompute_prefix_origin(prefix);
}

void RoutingState::withdraw(const std::string& peer,
                            const std::string& prefix) {
    const auto it = by_prefix_.find(prefix);
    if (it == by_prefix_.end()) {
        return;
    }

    it->second.erase(peer);

    if (it->second.empty()) {
        by_prefix_.erase(it);
        aggregated_.erase(prefix);
        return;
    }

    recompute_prefix_origin(prefix);
}

void RoutingState::export_tables(const std::string& prefix_file,
                                 const std::string& asn_file) const {
    std::ofstream prefix_stream(prefix_file);
    std::ofstream asn_stream(asn_file);

    std::unordered_map<uint32_t, std::vector<std::string>> asn_map;

    for (const auto& [prefix, asn] : aggregated_) {
        prefix_stream << prefix << '\t' << asn << '\n';
        asn_map[asn].push_back(prefix);
    }

    for (const auto& [asn, prefixes] : asn_map) {
        asn_stream << asn;

        for (const auto& prefix : prefixes) {
            asn_stream << '\t' << prefix;
        }

        asn_stream << '\n';
    }
}

void RoutingState::recompute_prefix_origin(const std::string& prefix) {
    std::unordered_map<uint32_t, int> counts;

    for (const auto& [peer, observation] : by_prefix_[prefix]) {
        static_cast<void>(peer);
        counts[observation.asn]++;
    }

    uint32_t best_asn = 0;
    int best_count = 0;

    for (const auto& [asn, count] : counts) {
        if (count > best_count) {
            best_count = count;
            best_asn = asn;
        }
    }

    aggregated_[prefix] = best_asn;
}
