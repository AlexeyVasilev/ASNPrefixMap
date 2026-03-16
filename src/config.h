#pragma once

#include <string>

struct Config {
    std::string source;
    std::string input_file;
    std::string prefix_output;
    std::string asn_output;
    std::string ris_live_host;
    std::string ris_live_port;
    std::string ris_live_target;
    std::size_t max_messages;
    std::string snapshot_input;
    std::string snapshot_output;
    bool stats_output_enabled;
    std::string stats_output_file;
    std::size_t stats_interval_ms;
    bool stop_on_keypress;
};

Config load_config(const std::string& path);
