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
};

Config load_config(const std::string& path);
