#include "file_jsonl_source.h"

#include <stdexcept>

FileJsonlSource::FileJsonlSource(const std::string& path)
    : input_(path) {
    if (!input_) {
        throw std::runtime_error("Failed to open input file: " + path);
    }
}

bool FileJsonlSource::next_message(std::string& json) {
    while (std::getline(input_, json)) {
        if (!json.empty()) {
            return true;
        }
    }

    return false;
}
