#pragma once

#include "bgp_source.h"

#include <fstream>
#include <string>

// Replay source for raw RIS Live JSON messages stored one per line.
// It intentionally does not read already-normalized announce/withdraw events.
class FileJsonlSource : public BgpSource {
public:
    explicit FileJsonlSource(const std::string& path);

    bool next_message(std::string& json) override;

private:
    std::ifstream input_;
};
