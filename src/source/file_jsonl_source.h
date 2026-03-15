#pragma once

#include "bgp_source.h"

#include <fstream>
#include <string>

class FileJsonlSource : public BgpSource {
public:
    explicit FileJsonlSource(const std::string& path);

    bool next_message(std::string& json) override;

private:
    std::ifstream input_;
};
