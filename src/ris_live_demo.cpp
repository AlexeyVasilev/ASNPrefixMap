#include "parser/ris_live_parser.h"
#include "peer/peer_registry.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {

const char* event_type_to_string(EventType type) {
    return type == EventType::Announce ? "announce" : "withdraw";
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string input_path = argc > 1 ? argv[1] : "examples/ris_live_messages.jsonl";

    std::ifstream input(input_path);
    if (!input) {
        std::cerr << "Failed to open input file: " << input_path << '\n';
        return 1;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const std::size_t emitted_events = parse_ris_live_message(
            line,
            [&](EventType type,
                const PeerInfo& peer,
                const std::string& prefix,
                uint32_t origin_asn,
                std::uint64_t timestamp) {
            std::cout << event_type_to_string(type)
                      << "\tpeer=" << PeerRegistry::make_key(peer)
                      << "\tprefix=" << prefix
                      << "\tasn=" << origin_asn
                      << "\ttimestamp=" << timestamp
                      << '\n';
        });
        std::cout << "message -> " << emitted_events << " event(s)\n";
    }

    return 0;
}
