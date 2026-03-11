#pragma once

#include <string>

struct Config {
    std::string source;
    std::string input_file;
    std::string prefix_output;
    std::string asn_output;
};

Config load_config(const std::string& path);
