#include "bgp_event.h"
#include "error_format.h"
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
#include <chrono>
#include <csignal>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_signal_stop_requested = false;

void handle_shutdown_signal(int signal_number) {
    static_cast<void>(signal_number);
    // Signal handlers must stay minimal and async-signal-safe. Only set a flag here.
    g_signal_stop_requested.store(true);
}

bool shutdown_requested(const std::atomic<bool>& stop_requested) {
    return stop_requested.load() || g_signal_stop_requested.load();
}

struct IngestionStats {
    std::size_t raw_messages_received = 0;
    std::size_t parsed_events_total = 0;
    std::size_t announces_applied = 0;
    std::size_t withdraws_applied = 0;
    std::size_t ignored_messages = 0;
};

std::string make_stats_filename() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream out;
    out << "stats_" << std::put_time(&local_time, "%Y-%m-%d_%H%M%S") << ".csv";
    return out.str();
}

void print_plateau_message(const GrowthSample& sample) {
    std::cerr << "[plateau] uptime_sec=" << sample.uptime_sec
              << " uptime_hms=" << format_duration_hms(sample.uptime_sec) << '\n';
    std::cerr << "[plateau] active_prefixes=" << sample.total_active_prefixes
              << " total_unique_asns_ever_seen=" << sample.total_unique_asns_ever_seen << '\n';
    std::cerr << "[plateau] note=table appears broadly complete; you may stop if a stable mapping is enough"
              << '\n';
}

std::unique_ptr<BgpSource> create_source(const Config& cfg,
                                         const std::atomic<bool>& stop_requested) {
    if (cfg.source == "file_jsonl") {
        return std::make_unique<FileJsonlSource>(cfg.input_file);
    }

    if (cfg.source == "ris_live_ws") {
        return std::make_unique<RisLiveWebSocketSource>(
            cfg.ris_live_host,
            cfg.ris_live_port,
            cfg.ris_live_target,
            cfg.reconnect_enabled,
            cfg.reconnect_initial_delay_ms,
            cfg.reconnect_max_delay_ms,
            cfg.reconnect_max_attempts,
            [&stop_requested]() { return shutdown_requested(stop_requested); });
    }

    throw std::runtime_error("Unsupported source: " + cfg.source);
}

void apply_event(const BgpEvent& event,
                 PeerRegistry& registry,
                 RoutingState& state,
                 IngestionStats& stats,
                 GrowthStatsTracker& growth_stats) {
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
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);

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
        std::unique_ptr<StatsCsvWriter> stats_writer;
        std::chrono::steady_clock::time_point next_stats_sample_at{};

        if (!cfg.snapshot_input.empty() && std::filesystem::exists(cfg.snapshot_input)) {
            const SnapshotStats snapshot_stats = SnapshotIO::load_snapshot(cfg.snapshot_input, peer_registry, state);
            std::cerr << "[snapshot] loaded " << cfg.snapshot_input << '\n';
            std::cerr << "[snapshot] read peers=" << snapshot_stats.peers
                      << " observations=" << snapshot_stats.observations << '\n';
        }

        growth_stats.seed_from_state(state);

        if (cfg.stats_output_enabled) {
            const std::string stats_file = make_stats_filename();
            stats_writer = std::make_unique<StatsCsvWriter>(stats_file);
            std::cerr << "[stats] writing " << stats_file << '\n';
            next_stats_sample_at = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(cfg.stats_interval_ms);
        }

        if (cfg.stop_on_keypress) {
            std::cerr << "[info] press Enter to stop\n";
            std::thread([&stop_requested]() {
                std::string line;
                std::getline(std::cin, line);
                stop_requested = true;
            }).detach();
        }

        std::unique_ptr<BgpSource> source = create_source(cfg, stop_requested);

        // Cleanup is deliberately kept in normal control flow rather than inside the signal handler.
        // In the current synchronous design, a blocking source read may delay shutdown slightly
        // until next_message() returns and the loop can observe the stop flag.
        std::string message;
        bool stopped_by_signal = false;
        while (!shutdown_requested(stop_requested) && source->next_message(message)) {
            if (g_signal_stop_requested.load()) {
                stopped_by_signal = true;
                break;
            }

            ++stats.raw_messages_received;
            growth_stats.on_message_received();

            const std::size_t emitted_events = parse_ris_live_message(
                message,
                [&](const BgpEvent& event) {
                    // The parser now feeds events directly into the ingest path, so we avoid
                    // allocating a temporary vector before PeerRegistry/prefix/state processing.
                    apply_event(event, peer_registry, state, stats, growth_stats);
                });
            stats.parsed_events_total += emitted_events;
            growth_stats.on_parsed_events(emitted_events);

            if (emitted_events == 0) {
                ++stats.ignored_messages;
            } else {
                growth_stats.set_active_prefix_counts(
                    state.active_prefixes_v4_count(),
                    state.active_prefixes_v6_count());
            }

            // The previous sampler thread needed mutexes because sampling raced with ingest updates.
            // In the current synchronous design, sampling directly from the main loop is a better fit:
            // it removes lock overhead from hot-path stats updates while keeping the same CSV output.
            if (cfg.stats_output_enabled && std::chrono::steady_clock::now() >= next_stats_sample_at) {
                const GrowthSample sample = growth_stats.sample_now();
                stats_writer->write_sample(sample);

                const PlateauStatus plateau = growth_stats.plateau_status();
                if (plateau.detected_on_this_sample) {
                    print_plateau_message(sample);
                }

                next_stats_sample_at = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(cfg.stats_interval_ms);
            }

            if (cfg.max_messages != 0 && stats.raw_messages_received >= cfg.max_messages) {
                break;
            }
        }

        if (!stopped_by_signal && g_signal_stop_requested.load()) {
            stopped_by_signal = true;
        }

        if (stopped_by_signal) {
            std::cerr << "[signal] shutdown requested\n";
            std::cerr << "[signal] saving current state\n";
        }

        if (cfg.stats_output_enabled) {
            const GrowthSample final_sample = growth_stats.sample_now();
            stats_writer->write_sample(final_sample);
            const PlateauStatus plateau = growth_stats.plateau_status();
            if (plateau.detected_on_this_sample) {
                print_plateau_message(final_sample);
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
        std::cerr << "Error: " << format_exception_message(ex) << '\n';
        return 1;
    }
}


