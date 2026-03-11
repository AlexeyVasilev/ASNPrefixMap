#include "config.h"
#include "routing_state.h"

#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    try {
        const Config cfg = load_config("config.ini");
        RoutingState state;

        std::ifstream input(cfg.input_file);
        if (!input) {
            std::cerr << "Failed to open input file: " << cfg.input_file << '\n';
            return 1;
        }

        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }

            const auto event = json::parse(line);

            const std::string type = event.at("type").get<std::string>();
            const std::string peer = event.at("peer").get<std::string>();
            const std::string prefix = event.at("prefix").get<std::string>();

            if (type == "announce") {
                const uint32_t asn = event.at("asn").get<uint32_t>();
                state.announce(peer, prefix, asn);
            } else if (type == "withdraw") {
                state.withdraw(peer, prefix);
            }
        }

        state.export_tables(cfg.prefix_output, cfg.asn_output);
        std::cout << "Done\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
