#pragma once

#include "prefix/prefix.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <vector>

struct GrowthSample {
    std::uint64_t timestamp_ms = 0;
    double uptime_sec = 0.0;
    std::size_t total_unique_asns_ever_seen = 0;
    std::size_t total_unique_prefixes_ever_seen = 0;
    std::size_t total_active_prefixes_v4 = 0;
    std::size_t total_active_prefixes_v6 = 0;
    std::size_t total_active_prefixes = 0;
    std::size_t new_asns_in_interval = 0;
    std::size_t new_prefixes_in_interval = 0;
    double new_asns_per_sec = 0.0;
    double new_prefixes_per_sec = 0.0;
    std::size_t raw_messages_received = 0;
    std::size_t parsed_events_total = 0;
    std::size_t announces_applied = 0;
    std::size_t withdraws_applied = 0;
};

class RoutingState;

class GrowthStatsTracker {
public:
    GrowthStatsTracker();

    void seed_from_state(const RoutingState& state);
    void on_message_received();
    void on_parsed_events(std::size_t parsed_events);
    void on_announce(uint32_t asn, const PrefixV4& prefix);
    void on_announce(uint32_t asn, const PrefixV6& prefix);
    void on_withdraw(const PrefixV4& prefix);
    void on_withdraw(const PrefixV6& prefix);
    void set_active_prefix_counts(std::size_t active_v4, std::size_t active_v6);
    GrowthSample sample_now();

private:
    std::mutex mutex_;
    std::unordered_set<uint32_t> ever_seen_asns_;
    std::unordered_set<PrefixV4, PrefixV4Hash> ever_seen_prefixes_v4_;
    std::unordered_set<PrefixV6, PrefixV6Hash> ever_seen_prefixes_v6_;
    std::size_t raw_messages_received_ = 0;
    std::size_t parsed_events_total_ = 0;
    std::size_t announces_applied_ = 0;
    std::size_t withdraws_applied_ = 0;
    std::size_t active_prefixes_v4_ = 0;
    std::size_t active_prefixes_v6_ = 0;
    std::size_t last_sample_asn_count_ = 0;
    std::size_t last_sample_prefix_count_ = 0;
    std::chrono::steady_clock::time_point started_at_;
    std::chrono::steady_clock::time_point last_sample_at_;
};

class StatsCsvWriter {
public:
    explicit StatsCsvWriter(const std::string& path);
    void write_sample(const GrowthSample& sample);
    void flush();

private:
    std::ofstream output_;
};
