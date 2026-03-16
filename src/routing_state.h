#pragma once

#include "peer/peer_registry.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Observation {
    PeerId peer_id;
    uint32_t origin_asn;
    std::uint64_t timestamp;
};

struct PrefixRecord {
    // Prefix is still kept as std::string in this step to keep the refactor incremental.
    // The main runtime win here comes from deduplicating peer identity via PeerId.
    std::vector<Observation> observations;
};

struct StoredObservation {
    std::string prefix;
    Observation observation;
};

class RoutingState {
public:
    void announce(PeerId peer_id,
                  const std::string& prefix,
                  uint32_t origin_asn,
                  std::uint64_t timestamp = 0);

    void withdraw(PeerId peer_id,
                  const std::string& prefix);

    void export_tables(const std::string& prefix_file,
                       const std::string& asn_file) const;

    std::vector<StoredObservation> stored_observations() const;
    void clear();
    void restore_observation(PeerId peer_id,
                             const std::string& prefix,
                             uint32_t origin_asn,
                             std::uint64_t timestamp);
    void rebuild_aggregated();

private:
    void recompute_prefix_origin(const std::string& prefix);

    std::unordered_map<std::string, PrefixRecord> by_prefix_;
    std::unordered_map<std::string, uint32_t> aggregated_;
};
