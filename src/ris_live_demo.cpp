#include "bgp_event.h"
#include "parser/ris_live_parser.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

        const std::vector<BgpEvent> events = parse_ris_live_message(line);
        std::cout << "message -> " << events.size() << " event(s)\n";

        for (const auto& event : events) {
            std::cout << event_type_to_string(event.type)
                      << "\tpeer=" << event.peer
                      << "\tprefix=" << event.prefix
                      << "\tasn=" << event.asn
                      << "\ttimestamp=" << event.timestamp
                      << '\n';
        }
    }

    return 0;
}
