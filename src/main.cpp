#include "bgp_event.h"
#include "config.h"
#include "parser/ris_live_parser.h"
#include "peer/peer_registry.h"
#include "prefix/prefix.h"
#include "routing_state.h"
#include "snapshot/snapshot_io.h"
#include "source/bgp_source.h"
#include "source/file_jsonl_source.h"
#include "source/ris_live_websocket_source.h"
#include "stats/growth_stats.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct IngestionStats {
    std::size_t raw_messages_received = 0;
    std::size_t parsed_events_total = 0;
    std::size_t announces_applied = 0;
    std::size_t withdraws_applied = 0;
    std::size_t ignored_messages = 0;
};

std::unique_ptr<BgpSource> create_source(const Config& cfg) {
    if (cfg.source == "file_jsonl") {
        return std::make_unique<FileJsonlSource>(cfg.input_file);
    }

    if (cfg.source == "ris_live_ws") {
        return std::make_unique<RisLiveWebSocketSource>(
            cfg.ris_live_host,
            cfg.ris_live_port,
            cfg.ris_live_target);
    }

    throw std::runtime_error("Unsupported source: " + cfg.source);
}

void apply_events(const std::vector<BgpEvent>& events,
                  PeerRegistry& registry,
                  RoutingState& state,
                  IngestionStats& stats,
                  GrowthStatsTracker& growth_stats) {
    for (const auto& event : events) {
        const PeerId peer_id = registry.get_or_add(event.peer);
        const BinaryPrefix prefix = parse_prefix(event.prefix);

        std::visit(
            [&](const auto& binary_prefix) {
                if (event.type == EventType::Announce) {
                    state.announce(peer_id, binary_prefix, event.asn, event.timestamp);
                    ++stats.announces_applied;
                    growth_stats.on_announce(event.asn, binary_prefix);
                } else if (event.type == EventType::Withdraw) {
                    state.withdraw(peer_id, binary_prefix);
                    ++stats.withdraws_applied;
                    growth_stats.on_withdraw(binary_prefix);
                }
            },
            prefix);
    }

    growth_stats.set_active_prefix_counts(
        state.active_prefixes_v4_count(),
        state.active_prefixes_v6_count());
}

void print_stats(const IngestionStats& stats) {
    std::cout << "raw_messages_received=" << stats.raw_messages_received << '\n';
    std::cout << "parsed_events_total=" << stats.parsed_events_total << '\n';
    std::cout << "announces_applied=" << stats.announces_applied << '\n';
    std::cout << "withdraws_applied=" << stats.withdraws_applied << '\n';
    std::cout << "ignored_messages=" << stats.ignored_messages << '\n';
}

}  // namespace

int main() {
    try {
        const Config cfg = load_config("config.ini");
        const PlateauSettings plateau_settings{
            cfg.plateau_detection_enabled,
            cfg.plateau_window_samples,
            cfg.plateau_prefix_rate_threshold,
            cfg.plateau_min_runtime_sec,
        };

        PeerRegistry peer_registry;
        RoutingState state;
        IngestionStats stats;
        GrowthStatsTracker growth_stats(plateau_settings);
        std::atomic<bool> stop_requested = false;
        std::atomic<bool> sampler_running = false;
        std::unique_ptr<StatsCsvWriter> stats_writer;
        std::thread sampler_thread;

        if (!cfg.snapshot_input.empty() && std::filesystem::exists(cfg.snapshot_input)) {
            const SnapshotStats snapshot_stats = SnapshotIO::load_snapshot(cfg.snapshot_input, peer_registry, state);
            std::cerr << "[snapshot] loaded " << cfg.snapshot_input << '\n';
            std::cerr << "[snapshot] read peers=" << snapshot_stats.peers
                      << " observations=" << snapshot_stats.observations << '\n';
        }

        growth_stats.seed_from_state(state);

        if (cfg.stats_output_enabled) {
            stats_writer = std::make_unique<StatsCsvWriter>(cfg.stats_output_file);
            sampler_running = true;

            sampler_thread = std::thread([&]() {
                while (sampler_running.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.stats_interval_ms));
                    if (!sampler_running.load()) {
                        break;
                    }

                    const GrowthSample sample = growth_stats.sample_now();
                    stats_writer->write_sample(sample);

                    const PlateauStatus plateau = growth_stats.plateau_status();
                    if (plateau.detected_on_this_sample) {
                        std::cerr << "[plateau] uptime_sec=" << sample.uptime_sec
                                  << " uptime_hms=" << format_duration_hms(sample.uptime_sec)
                                  << " active_prefixes=" << sample.total_active_prefixes
                                  << " total_unique_asns_ever_seen=" << sample.total_unique_asns_ever_seen
                                  << " note=table appears broadly complete; you may stop if a stable mapping is enough"
                                  << '\n';
                    }
                }
            });
        }

        if (cfg.stop_on_keypress) {
            std::cerr << "[info] press Enter to stop\n";
            // Plateau detection only notifies; it does not stop automatically because the signal
            // is heuristic and operator intent should remain explicit.
            std::thread([&stop_requested]() {
                std::string line;
                std::getline(std::cin, line);
                stop_requested = true;
            }).detach();
        }

        std::unique_ptr<BgpSource> source = create_source(cfg);

        std::string message;
        while (!stop_requested.load() && source->next_message(message)) {
            ++stats.raw_messages_received;
            growth_stats.on_message_received();

            const std::vector<BgpEvent> events = parse_ris_live_message(message);
            stats.parsed_events_total += events.size();
            growth_stats.on_parsed_events(events.size());

            if (events.empty()) {
                ++stats.ignored_messages;
            } else {
                apply_events(events, peer_registry, state, stats, growth_stats);
            }

            if (cfg.max_messages != 0 && stats.raw_messages_received >= cfg.max_messages) {
                break;
            }
        }

        if (cfg.stats_output_enabled) {
            sampler_running = false;
            if (sampler_thread.joinable()) {
                sampler_thread.join();
            }

            const GrowthSample final_sample = growth_stats.sample_now();
            stats_writer->write_sample(final_sample);
            const PlateauStatus plateau = growth_stats.plateau_status();
            if (plateau.detected_on_this_sample) {
                std::cerr << "[plateau] uptime_sec=" << final_sample.uptime_sec
                          << " uptime_hms=" << format_duration_hms(final_sample.uptime_sec)
                          << " active_prefixes=" << final_sample.total_active_prefixes
                          << " total_unique_asns_ever_seen=" << final_sample.total_unique_asns_ever_seen
                          << " note=table appears broadly complete; you may stop if a stable mapping is enough"
                          << '\n';
            }
            stats_writer->flush();
        }

        print_stats(stats);
        const double runtime_sec = growth_stats.runtime_sec();
        std::cout << "runtime_sec=" << runtime_sec << '\n';
        std::cout << "runtime_hms=" << format_duration_hms(runtime_sec) << '\n';

        const PlateauStatus plateau = growth_stats.plateau_status();
        if (plateau.detected) {
            std::cout << "plateau_detected=true\n";
            std::cout << "plateau_uptime_sec=" << plateau.plateau_uptime_sec << '\n';
            std::cout << "plateau_uptime_hms=" << format_duration_hms(plateau.plateau_uptime_sec) << '\n';
        } else {
            std::cout << "plateau_detected=false\n";
        }

        const SnapshotStats snapshot_stats = SnapshotIO::save_snapshot(cfg.snapshot_output, peer_registry, state);
        std::cerr << "[snapshot] saved " << cfg.snapshot_output << '\n';
        std::cerr << "[snapshot] wrote peers=" << snapshot_stats.peers
                  << " observations=" << snapshot_stats.observations << '\n';
        state.export_tables(cfg.prefix_output, cfg.asn_output);
        std::cout << "Done\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
