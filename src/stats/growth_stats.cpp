#include "growth_stats.h"

#include "routing_state.h"

#include <iomanip>
#include <stdexcept>

namespace {

std::uint64_t current_timestamp_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

GrowthStatsTracker::GrowthStatsTracker()
    : started_at_(std::chrono::steady_clock::now()),
      last_sample_at_(started_at_) {
}

void GrowthStatsTracker::seed_from_state(const RoutingState& state) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& stored : state.stored_observations()) {
        ever_seen_asns_.insert(stored.observation.origin_asn);
        ever_seen_prefixes_.insert(to_string(stored.prefix));
    }

    active_prefixes_v4_ = state.active_prefixes_v4_count();
    active_prefixes_v6_ = state.active_prefixes_v6_count();
    last_sample_asn_count_ = ever_seen_asns_.size();
    last_sample_prefix_count_ = ever_seen_prefixes_.size();
}

void GrowthStatsTracker::on_message_received() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++raw_messages_received_;
}

void GrowthStatsTracker::on_parsed_events(std::size_t parsed_events) {
    std::lock_guard<std::mutex> lock(mutex_);
    parsed_events_total_ += parsed_events;
}

void GrowthStatsTracker::on_announce(uint32_t asn, const std::string& prefix_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_asns_.insert(asn);
    ever_seen_prefixes_.insert(prefix_text);
    ++announces_applied_;
}

void GrowthStatsTracker::on_withdraw(const std::string& prefix_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_prefixes_.insert(prefix_text);
    ++withdraws_applied_;
}

void GrowthStatsTracker::set_active_prefix_counts(std::size_t active_v4, std::size_t active_v6) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_prefixes_v4_ = active_v4;
    active_prefixes_v6_ = active_v6;
}

GrowthSample GrowthStatsTracker::sample_now() {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = std::chrono::steady_clock::now();
    const double uptime_sec = std::chrono::duration<double>(now - started_at_).count();
    const double interval_sec = std::chrono::duration<double>(now - last_sample_at_).count();
    const std::size_t total_asns = ever_seen_asns_.size();
    const std::size_t total_prefixes = ever_seen_prefixes_.size();
    const std::size_t new_asns = total_asns - last_sample_asn_count_;
    const std::size_t new_prefixes = total_prefixes - last_sample_prefix_count_;

    GrowthSample sample;
    sample.timestamp_ms = current_timestamp_ms();
    sample.uptime_sec = uptime_sec;
    sample.total_unique_asns_ever_seen = total_asns;
    sample.total_unique_prefixes_ever_seen = total_prefixes;
    sample.total_active_prefixes_v4 = active_prefixes_v4_;
    sample.total_active_prefixes_v6 = active_prefixes_v6_;
    sample.total_active_prefixes = active_prefixes_v4_ + active_prefixes_v6_;
    sample.new_asns_in_interval = new_asns;
    sample.new_prefixes_in_interval = new_prefixes;
    sample.new_asns_per_sec = interval_sec > 0.0 ? static_cast<double>(new_asns) / interval_sec : 0.0;
    sample.new_prefixes_per_sec = interval_sec > 0.0 ? static_cast<double>(new_prefixes) / interval_sec : 0.0;
    sample.raw_messages_received = raw_messages_received_;
    sample.parsed_events_total = parsed_events_total_;
    sample.announces_applied = announces_applied_;
    sample.withdraws_applied = withdraws_applied_;

    // Ever-seen counts stay separate from active counts because a prefix or ASN may disappear
    // from active state and later reappear; growth estimation needs lifetime-first-seen tracking.
    last_sample_asn_count_ = total_asns;
    last_sample_prefix_count_ = total_prefixes;
    last_sample_at_ = now;
    return sample;
}

StatsCsvWriter::StatsCsvWriter(const std::string& path)
    : output_(path, std::ios::app) {
    if (!output_) {
        throw std::runtime_error("Failed to open stats output file: " + path);
    }

    output_.seekp(0, std::ios::end);
    if (output_.tellp() == 0) {
        output_ << "timestamp_ms,uptime_sec,total_unique_asns_ever_seen,total_unique_prefixes_ever_seen,"
                << "total_active_prefixes_v4,total_active_prefixes_v6,total_active_prefixes,"
                << "new_asns_in_interval,new_prefixes_in_interval,new_asns_per_sec,new_prefixes_per_sec,"
                << "raw_messages_received,parsed_events_total,announces_applied,withdraws_applied\n";
    }
}

void StatsCsvWriter::write_sample(const GrowthSample& sample) {
    output_ << sample.timestamp_ms << ','
            << std::fixed << std::setprecision(3) << sample.uptime_sec << ','
            << sample.total_unique_asns_ever_seen << ','
            << sample.total_unique_prefixes_ever_seen << ','
            << sample.total_active_prefixes_v4 << ','
            << sample.total_active_prefixes_v6 << ','
            << sample.total_active_prefixes << ','
            << sample.new_asns_in_interval << ','
            << sample.new_prefixes_in_interval << ','
            << std::fixed << std::setprecision(6) << sample.new_asns_per_sec << ','
            << std::fixed << std::setprecision(6) << sample.new_prefixes_per_sec << ','
            << sample.raw_messages_received << ','
            << sample.parsed_events_total << ','
            << sample.announces_applied << ','
            << sample.withdraws_applied << '\n';
    output_.flush();
}

void StatsCsvWriter::flush() {
    output_.flush();
}
