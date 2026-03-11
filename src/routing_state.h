#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Observation {
    uint32_t asn;
};

class RoutingState {
public:
    void announce(const std::string& peer,
                  const std::string& prefix,
                  uint32_t asn);

    void withdraw(const std::string& peer,
                  const std::string& prefix);

    void export_tables(const std::string& prefix_file,
                       const std::string& asn_file) const;

private:
    void recompute_prefix_origin(const std::string& prefix);

    std::unordered_map<std::string, std::unordered_map<std::string, Observation>> by_prefix_;
    std::unordered_map<std::string, uint32_t> aggregated_;
};
