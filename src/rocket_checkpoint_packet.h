// GPL-3.0-or-later, part of the RocketRPG Player modification.
#pragma once
#include "rocket_checkpoint_json.h"
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace RocketCheckpoint {
constexpr size_t MaxBytes = 16 * 1024 * 1024;
using Json = nlohmann::json;

inline Json Parse(std::string_view text) {
    if (text.empty() || text.size() > MaxBytes) throw std::runtime_error("Checkpoint size limit exceeded");
    size_t nodes = 0;
    std::vector<std::unordered_set<std::string>> keys;
    return Json::parse(text.begin(), text.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 100) throw std::runtime_error("Checkpoint depth limit exceeded");
        if (event == Json::parse_event_t::value || event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start)
            if (++nodes > 200000) throw std::runtime_error("Checkpoint object limit exceeded");
        if (event == Json::parse_event_t::value && (value.is_null() || (value.is_number_float() && !std::isfinite(value.get<double>()))))
            throw std::runtime_error("Invalid checkpoint value");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate checkpoint property");
        return true;
    });
}

inline lcf::rpg::Save ReadState(const Json& value) {
    auto save = value.get<lcf::rpg::Save>();
    if (Json(save) != value) throw std::runtime_error("Checkpoint values cannot be represented by this engine");
    if (!save.foreground_event_execstate.stack.empty()) throw std::runtime_error("Active interpreter cannot be restored");
    return save;
}
} // namespace RocketCheckpoint
