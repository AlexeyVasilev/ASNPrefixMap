#include "config.h"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(std::string value) {
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }

    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    return value;
}

}  // namespace

Config load_config(const std::string& path) {
    Config cfg;
    cfg.source = "file_jsonl";
    cfg.ris_live_host = "ris-live.ripe.net";
    cfg.ris_live_port = "443";
    cfg.ris_live_target = "/v1/ws/";

    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));

        if (key == "source") {
            cfg.source = value;
        } else if (key == "input_file") {
            cfg.input_file = value;
        } else if (key == "prefix_output") {
            cfg.prefix_output = value;
        } else if (key == "asn_output") {
            cfg.asn_output = value;
        } else if (key == "ris_live_host") {
            cfg.ris_live_host = value;
        } else if (key == "ris_live_port") {
            cfg.ris_live_port = value;
        } else if (key == "ris_live_target") {
            cfg.ris_live_target = value;
        }
    }

    return cfg;
}
