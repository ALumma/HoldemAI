#pragma once

#include "holdem/types.hpp"

#include <span>

namespace holdem {

struct HandRank {
    // Adapter normalizes the chosen evaluator's ordering: larger is stronger.
    std::uint32_t value;
};

// Replaceable adapter around a validated five-to-seven-card evaluator.
class HandEvaluator {
public:
    virtual ~HandEvaluator() = default;
    [[nodiscard]] virtual HandRank evaluate(std::span<const Card> cards) const = 0;
};

} // namespace holdem
