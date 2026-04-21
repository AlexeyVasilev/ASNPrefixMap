#pragma once

#include "../bgp_event.h"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace ris_live_parser_detail {

using EventSinkFn = void(*)(void* context, const BgpEvent& event);

std::size_t parse_ris_live_message_impl(const std::string& text,
                                        void* context,
                                        EventSinkFn on_event);

}  // namespace ris_live_parser_detail

template <typename Fn>
std::size_t parse_ris_live_message(const std::string& text, Fn&& on_event) {
    // The ingest pipeline consumes events immediately, so emitting them directly avoids
    // allocating a temporary std::vector<BgpEvent> for every raw RIS Live message.
    using Callback = std::remove_reference_t<Fn>;
    Callback& callback = on_event;

    auto thunk = [](void* context, const BgpEvent& event) {
        (*static_cast<Callback*>(context))(event);
    };

    return ris_live_parser_detail::parse_ris_live_message_impl(text, &callback, thunk);
}
