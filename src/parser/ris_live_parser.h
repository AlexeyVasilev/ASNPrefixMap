#pragma once

#include "../bgp_event.h"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace ris_live_parser_detail {

using EventSinkFn = void(*)(void* context,
                            EventType type,
                            const PeerInfo& peer,
                            const std::string& prefix,
                            uint32_t origin_asn,
                            std::uint64_t timestamp);

std::size_t parse_ris_live_message_impl(const std::string& text,
                                        void* context,
                                        EventSinkFn on_event);

}  // namespace ris_live_parser_detail

template <typename Fn>
std::size_t parse_ris_live_message(const std::string& text, Fn&& on_event) {
    // The ingest pipeline consumes parsed fields immediately, so pass them straight through
    // instead of materializing a temporary BgpEvent for each emitted event.
    using Callback = std::remove_reference_t<Fn>;
    Callback& callback = on_event;

    auto thunk = [](void* context,
                    EventType type,
                    const PeerInfo& peer,
                    const std::string& prefix,
                    uint32_t origin_asn,
                    std::uint64_t timestamp) {
        (*static_cast<Callback*>(context))(type, peer, prefix, origin_asn, timestamp);
    };

    return ris_live_parser_detail::parse_ris_live_message_impl(text, &callback, thunk);
}
