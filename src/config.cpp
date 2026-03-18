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

bool parse_bool(const std::string& value) {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }

    throw std::runtime_error("Invalid boolean value in config: " + value);
}

}  // namespace

Config load_config(const std::string& path) {
    Config cfg;
    cfg.source = "file_jsonl";
    cfg.ris_live_host = "ris-live.ripe.net";
    cfg.ris_live_port = "443";
    cfg.ris_live_target = "/v1/ws/";
    cfg.max_messages = 0;
    cfg.snapshot_output = "snapshot.txt";
    cfg.stats_output_enabled = false;
    cfg.stats_interval_ms = 1000;
    cfg.stop_on_keypress = false;
    cfg.plateau_detection_enabled = true;
    cfg.plateau_window_samples = 300;
    cfg.plateau_prefix_rate_threshold = 5.0;
    cfg.plateau_min_runtime_sec = 600.0;

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
        } else if (key == "max_messages") {
            cfg.max_messages = static_cast<std::size_t>(std::stoull(value));
        } else if (key == "snapshot_input") {
            cfg.snapshot_input = value;
        } else if (key == "snapshot_output") {
            cfg.snapshot_output = value;
        } else if (key == "stats_output_enabled") {
            cfg.stats_output_enabled = parse_bool(value);
        } else if (key == "stats_interval_ms") {
            cfg.stats_interval_ms = static_cast<std::size_t>(std::stoull(value));
        } else if (key == "stop_on_keypress") {
            cfg.stop_on_keypress = parse_bool(value);
        } else if (key == "plateau_detection_enabled") {
            cfg.plateau_detection_enabled = parse_bool(value);
        } else if (key == "plateau_window_samples") {
            cfg.plateau_window_samples = static_cast<std::size_t>(std::stoull(value));
        } else if (key == "plateau_prefix_rate_threshold") {
            cfg.plateau_prefix_rate_threshold = std::stod(value);
        } else if (key == "plateau_min_runtime_sec") {
            cfg.plateau_min_runtime_sec = std::stod(value);
        }
    }

    return cfg;
}
