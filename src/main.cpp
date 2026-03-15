#include "config.h"
#include "parser/ris_live_parser.h"
#include "routing_state.h"
#include "source/bgp_source.h"
#include "source/file_jsonl_source.h"
#include "source/ris_live_websocket_source.h"

#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

void apply_events(const std::vector<BgpEvent>& events, RoutingState& state) {
    for (const auto& event : events) {
        if (event.type == EventType::Announce) {
            state.announce(event.peer, event.prefix, event.asn);
        } else if (event.type == EventType::Withdraw) {
            state.withdraw(event.peer, event.prefix);
        }
    }
}

}  // namespace

int main() {
    try {
        const Config cfg = load_config("config.ini");
        RoutingState state;
        std::unique_ptr<BgpSource> source = create_source(cfg);

        std::string message;
        while (source->next_message(message)) {
            const std::vector<BgpEvent> events = parse_ris_live_message(message);
            apply_events(events, state);
        }

        state.export_tables(cfg.prefix_output, cfg.asn_output);
        std::cout << "Done\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
