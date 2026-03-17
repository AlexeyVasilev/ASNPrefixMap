#include "growth_stats.h"

#include "routing_state.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {

std::uint64_t current_timestamp_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

GrowthStatsTracker::GrowthStatsTracker(const PlateauSettings& plateau_settings)
    : plateau_settings_(plateau_settings),
      started_at_(std::chrono::steady_clock::now()),
      last_sample_at_(started_at_) {
}

void GrowthStatsTracker::seed_from_state(const RoutingState& state) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& stored : state.stored_observations()) {
        ever_seen_asns_.insert(stored.observation.origin_asn);
        std::visit(
            [&](const auto& prefix) {
                using PrefixType = std::decay_t<decltype(prefix)>;
                if constexpr (std::is_same_v<PrefixType, PrefixV4>) {
                    ever_seen_prefixes_v4_.insert(prefix);
                } else {
                    ever_seen_prefixes_v6_.insert(prefix);
                }
            },
            stored.prefix);
    }

    active_prefixes_v4_ = state.active_prefixes_v4_count();
    active_prefixes_v6_ = state.active_prefixes_v6_count();
    last_sample_asn_count_ = ever_seen_asns_.size();
    last_sample_prefix_count_ = ever_seen_prefixes_v4_.size() + ever_seen_prefixes_v6_.size();
}

void GrowthStatsTracker::on_message_received() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++raw_messages_received_;
}

void GrowthStatsTracker::on_parsed_events(std::size_t parsed_events) {
    std::lock_guard<std::mutex> lock(mutex_);
    parsed_events_total_ += parsed_events;
}

void GrowthStatsTracker::on_announce(uint32_t asn, const PrefixV4& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_asns_.insert(asn);
    ever_seen_prefixes_v4_.insert(prefix);
    ++announces_applied_;
}

void GrowthStatsTracker::on_announce(uint32_t asn, const PrefixV6& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_asns_.insert(asn);
    ever_seen_prefixes_v6_.insert(prefix);
    ++announces_applied_;
}

void GrowthStatsTracker::on_withdraw(const PrefixV4& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_prefixes_v4_.insert(prefix);
    ++withdraws_applied_;
}

void GrowthStatsTracker::on_withdraw(const PrefixV6& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    ever_seen_prefixes_v6_.insert(prefix);
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
    const std::size_t total_prefixes = ever_seen_prefixes_v4_.size() + ever_seen_prefixes_v6_.size();
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

    // Plateau detection is heuristic, not a proof of completeness. A rolling average is used
    // instead of a single raw sample because instantaneous growth rates are noisy.
    plateau_detected_on_last_sample_ = false;
    if (plateau_settings_.enabled && plateau_settings_.window_samples > 0) {
        recent_prefix_rates_.push_back(sample.new_prefixes_per_sec);
        recent_prefix_rate_sum_ += sample.new_prefixes_per_sec;
        while (recent_prefix_rates_.size() > plateau_settings_.window_samples) {
            recent_prefix_rate_sum_ -= recent_prefix_rates_.front();
            recent_prefix_rates_.pop_front();
        }

        if (!plateau_detected_ &&
            recent_prefix_rates_.size() == plateau_settings_.window_samples &&
            uptime_sec >= plateau_settings_.min_runtime_sec) {
            const double average_rate = recent_prefix_rate_sum_ /
                static_cast<double>(recent_prefix_rates_.size());
            if (average_rate < plateau_settings_.prefix_rate_threshold) {
                plateau_detected_ = true;
                plateau_uptime_sec_ = uptime_sec;
                plateau_detected_on_last_sample_ = true;
            }
        }
    }

    last_sample_asn_count_ = total_asns;
    last_sample_prefix_count_ = total_prefixes;
    last_sample_at_ = now;
    return sample;
}

PlateauStatus GrowthStatsTracker::plateau_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return PlateauStatus{
        plateau_detected_,
        plateau_detected_on_last_sample_,
        plateau_uptime_sec_,
    };
}

double GrowthStatsTracker::runtime_sec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at_).count();
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

std::string format_duration_hms(double total_seconds) {
    const auto seconds = static_cast<long long>(std::floor(total_seconds));
    const long long hours = seconds / 3600;
    const long long minutes = (seconds % 3600) / 60;
    const long long secs = seconds % 60;

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':'
        << std::setw(2) << secs;
    return out.str();
}
