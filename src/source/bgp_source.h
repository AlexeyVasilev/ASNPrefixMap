#pragma once

#include <string>

class BgpSource {
public:
    virtual ~BgpSource() = default;
    virtual bool next_message(std::string& json) = 0;
};
