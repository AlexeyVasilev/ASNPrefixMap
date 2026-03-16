#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Observation {
    uint32_t asn;
    std::uint64_t timestamp;
};

struct StoredObservation {
    std::string prefix;
    std::string peer;
    Observation observation;
};

class RoutingState {
public:
    void announce(const std::string& peer,
                  const std::string& prefix,
                  uint32_t asn,
                  std::uint64_t timestamp = 0);

    void withdraw(const std::string& peer,
                  const std::string& prefix);

    void export_tables(const std::string& prefix_file,
                       const std::string& asn_file) const;

    std::vector<StoredObservation> stored_observations() const;
    void clear();
    void restore_observation(const std::string& peer,
                             const std::string& prefix,
                             uint32_t asn,
                             std::uint64_t timestamp);
    void rebuild_aggregated();

private:
    void recompute_prefix_origin(const std::string& prefix);

    std::unordered_map<std::string, std::unordered_map<std::string, Observation>> by_prefix_;
    std::unordered_map<std::string, uint32_t> aggregated_;
};
