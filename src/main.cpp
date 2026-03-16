#include "config.h"
#include "parser/ris_live_parser.h"
#include "routing_state.h"
#include "snapshot/snapshot_io.h"
#include "source/bgp_source.h"
#include "source/file_jsonl_source.h"
#include "source/ris_live_websocket_source.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
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
                  RoutingState& state,
                  IngestionStats& stats) {
    for (const auto& event : events) {
        if (event.type == EventType::Announce) {
            state.announce(event.peer, event.prefix, event.asn, event.timestamp);
            ++stats.announces_applied;
        } else if (event.type == EventType::Withdraw) {
            state.withdraw(event.peer, event.prefix);
            ++stats.withdraws_applied;
        }
    }
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
        RoutingState state;
        IngestionStats stats;

        if (!cfg.snapshot_input.empty() && std::filesystem::exists(cfg.snapshot_input)) {
            const SnapshotStats snapshot_stats = SnapshotIO::load_snapshot(cfg.snapshot_input, state);
            std::cerr << "[snapshot] loaded " << cfg.snapshot_input << '\n';
            std::cerr << "[snapshot] read peers=" << snapshot_stats.peers
                      << " observations=" << snapshot_stats.observations << '\n';
        }

        std::unique_ptr<BgpSource> source = create_source(cfg);

        // Data flow stays intentionally simple:
        // source -> raw JSON -> parser -> BgpEvent -> RoutingState
        std::string message;
        while (source->next_message(message)) {
            ++stats.raw_messages_received;

            const std::vector<BgpEvent> events = parse_ris_live_message(message);
            stats.parsed_events_total += events.size();

            if (events.empty()) {
                ++stats.ignored_messages;
            } else {
                apply_events(events, state, stats);
            }

            if (cfg.max_messages != 0 && stats.raw_messages_received >= cfg.max_messages) {
                break;
            }
        }

        print_stats(stats);
        const SnapshotStats snapshot_stats = SnapshotIO::save_snapshot(cfg.snapshot_output, state);
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
