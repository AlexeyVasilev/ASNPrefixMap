#pragma once

#include "../bgp_event.h"

#include <string>
#include <vector>

std::vector<BgpEvent> parse_ris_live_message(const std::string& text);
