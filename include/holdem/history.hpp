#pragma once

#include "holdem/hand_engine.hpp"

#include <string_view>
#include <variant>

namespace holdem {

// Full simulator record; may contain private cards. Do not feed to policies.
// Forced bets, recipient of each dealt card, and settlement derive from setup
// and engine rules. Record the rules version to keep replay interpretable.
struct HandRecord {
    std::string rules_version;
    HandSetup setup;
    std::vector<std::variant<Card, Action>> events;
    std::optional<HandResult> result;
};

class HistoryCodec {
public:
    virtual ~HistoryCodec() = default;
    // Intended external format: PHH. Internal engine state uses numeric values.
    [[nodiscard]] virtual std::string encode(const HandRecord&) const = 0;
    [[nodiscard]] virtual HandRecord decode(std::string_view text) const = 0;
};

} // namespace holdem
