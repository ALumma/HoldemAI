#pragma once

#include "holdem/tournament.hpp"

#include <span>

namespace holdem {

struct DecisionInput {
    PlayerObservation hand;
    TournamentObservation tournament;
};

// Bet-size abstraction belongs here, outside the rules engine. The engine must
// support every legal integer raise, even when training samples only a few.
class ActionAbstraction {
public:
    virtual ~ActionAbstraction() = default;
    [[nodiscard]] virtual std::vector<Action> candidates(const DecisionInput&) const = 0;
};

class Policy {
public:
    virtual ~Policy() = default;
    // One finite, nonnegative probability per candidate, summing to one.
    [[nodiscard]] virtual std::vector<double> probabilities(
        const DecisionInput&, std::span<const Action> candidates) const = 0;
};

// Future trainer: sampled CFR traversals, information-set encoding, regret and
// strategy buffers, batched neural inference, checkpoints, and evaluation.
// Tournament prize utility is separate from an individual hand's net_chips.

} // namespace holdem
