#include "ris_live_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::string get_string_or_empty(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return "";
    }

    return it->get<std::string>();
}

uint32_t get_uint32_or_zero(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return 0;
    }

    return it->get<uint32_t>();
}

std::uint64_t get_timestamp_or_zero(const json& object) {
    const auto it = object.find("timestamp");
    if (it == object.end() || !it->is_number_unsigned()) {
        return 0;
    }

    return it->get<std::uint64_t>();
}

PeerInfo build_peer_info(const json& data) {
    return PeerInfo{
        get_string_or_empty(data, "host"),
        get_string_or_empty(data, "peer"),
        get_uint32_or_zero(data, "peer_asn"),
    };
}

bool try_get_origin_asn(const json& data, uint32_t& origin_asn) {
    const auto path_it = data.find("path");
    if (path_it == data.end() || !path_it->is_array() || path_it->empty()) {
        return false;
    }

    const json& last = path_it->back();

    // Assumption: only a plain integer ASN is accepted as origin.
    // Compound values such as arrays, strings, or AS-SET-like data are skipped.
    if (!last.is_number_unsigned()) {
        return false;
    }

    origin_asn = last.get<uint32_t>();
    return true;
}

void append_announce_events(const json& data,
                            const PeerInfo& peer,
                            std::uint64_t timestamp,
                            std::vector<BgpEvent>& events) {
    uint32_t origin_asn = 0;
    if (!try_get_origin_asn(data, origin_asn)) {
        return;
    }

    const auto announcements_it = data.find("announcements");
    if (announcements_it == data.end() || !announcements_it->is_array()) {
        return;
    }

    for (const auto& announcement : *announcements_it) {
        const auto prefixes_it = announcement.find("prefixes");
        if (prefixes_it == announcement.end() || !prefixes_it->is_array()) {
            continue;
        }

        for (const auto& prefix_value : *prefixes_it) {
            if (!prefix_value.is_string()) {
                continue;
            }

            events.push_back(BgpEvent{
                EventType::Announce,
                peer,
                prefix_value.get<std::string>(),
                origin_asn,
                timestamp,
            });
        }
    }
}

void append_withdraw_events(const json& data,
                            const PeerInfo& peer,
                            std::uint64_t timestamp,
                            std::vector<BgpEvent>& events) {
    const auto withdrawals_it = data.find("withdrawals");
    if (withdrawals_it == data.end() || !withdrawals_it->is_array()) {
        return;
    }

    for (const auto& prefix_value : *withdrawals_it) {
        if (!prefix_value.is_string()) {
            continue;
        }

        events.push_back(BgpEvent{
            EventType::Withdraw,
            peer,
            prefix_value.get<std::string>(),
            0,
            timestamp,
        });
    }
}

}  // namespace

std::vector<BgpEvent> parse_ris_live_message(const std::string& text) {
    std::vector<BgpEvent> events;

    json outer;
    try {
        outer = json::parse(text);
    } catch (const json::parse_error&) {
        // Assumption: malformed messages are ignored and produce no events.
        return events;
    }

    if (outer.value("type", "") != "ris_message") {
        return events;
    }

    const auto data_it = outer.find("data");
    if (data_it == outer.end() || !data_it->is_object()) {
        return events;
    }

    const json& data = *data_it;
    if (data.value("type", "") != "UPDATE") {
        return events;
    }

    const PeerInfo peer = build_peer_info(data);
    const std::uint64_t timestamp = get_timestamp_or_zero(data);

    append_announce_events(data, peer, timestamp, events);
    append_withdraw_events(data, peer, timestamp, events);

    return events;
}
