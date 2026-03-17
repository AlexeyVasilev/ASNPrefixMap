#pragma once

#include <boost/container/small_vector.hpp>

#include "peer/peer_registry.h"
#include "prefix/prefix.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Observation {
    PeerId peer_id;
    uint32_t origin_asn;
};

struct PrefixRecord {
    // Most prefixes are typically observed from only a few peers, so a small inline capacity
    // avoids heap allocation on the common path while keeping the implementation simple.
    boost::container::small_vector<Observation, 4> observations;
    boost::container::small_vector<std::uint64_t, 4> timestamps;
};

struct StoredObservation {
    BinaryPrefix prefix;
    Observation observation;
    std::uint64_t timestamp;
};

class RoutingState {
public:
    void announce(PeerId peer_id,
                  const PrefixV4& prefix,
                  uint32_t origin_asn,
                  std::uint64_t timestamp = 0);
    void announce(PeerId peer_id,
                  const PrefixV6& prefix,
                  uint32_t origin_asn,
                  std::uint64_t timestamp = 0);

    void withdraw(PeerId peer_id,
                  const PrefixV4& prefix);
    void withdraw(PeerId peer_id,
                  const PrefixV6& prefix);

    void export_tables(const std::string& prefix_file,
                       const std::string& asn_file) const;

    std::vector<StoredObservation> stored_observations() const;
    std::size_t active_prefixes_v4_count() const;
    std::size_t active_prefixes_v6_count() const;
    std::size_t active_prefixes_total_count() const;
    void clear();
    void restore_observation(PeerId peer_id,
                             const PrefixV4& prefix,
                             uint32_t origin_asn,
                             std::uint64_t timestamp);
    void restore_observation(PeerId peer_id,
                             const PrefixV6& prefix,
                             uint32_t origin_asn,
                             std::uint64_t timestamp);
    void rebuild_aggregated();

private:
    void recompute_prefix_origin(const PrefixV4& prefix);
    void recompute_prefix_origin(const PrefixV6& prefix);

    // Binary prefixes avoid repeated CIDR strings in runtime state, normalize address bits,
    // and make later prefix-aware indexing possible. IPv4 and IPv6 stay in separate maps
    // because they are different key spaces and will eventually need different data structures.
    std::unordered_map<PrefixV4, PrefixRecord, PrefixV4Hash> by_prefix_v4_;
    std::unordered_map<PrefixV6, PrefixRecord, PrefixV6Hash> by_prefix_v6_;
    std::unordered_map<PrefixV4, uint32_t, PrefixV4Hash> aggregated_v4_;
    std::unordered_map<PrefixV6, uint32_t, PrefixV6Hash> aggregated_v6_;
};
