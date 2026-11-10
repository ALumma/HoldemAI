#pragma once

#include "holdem/types.hpp"

#include <memory>
#include <span>

namespace holdem {

struct HandSetup {
    HandId id = 0;
    Stakes stakes;
    Seat button = 0;
    std::vector<SeatAssignment> players;
};

// This is the only hand representation supplied to a decision-making policy.
// It must not expose opponent private cards, the deck, or simulator RNG state.
struct PlayerObservation {
    HandId hand;
    Seat viewer;
    Seat button;
    std::optional<Seat> actor;
    Street street;
    Stakes stakes;
    std::optional<HoleCards> hole_cards;
    std::vector<Card> board;
    std::vector<PublicPlayerState> players;
    std::vector<BettingEvent> betting_history;
    Chips pot; // Total committed chips, including the current street.
    LegalActions legal; // Empty unless viewer is the acting player.
};

struct ChanceOutcome {
    Card card;
    double probability;
};

// Simulator-owned state. The concrete implementation will contain private cards,
// remaining cards, contributions, pending actors, and betting-reopening state.
class HandState {
public:
    virtual ~HandState() = default;
    // Independent branch: mutation must never affect the parent state.
    [[nodiscard]] virtual std::unique_ptr<HandState> clone() const = 0;
    [[nodiscard]] virtual NodeKind node_kind() const = 0;
    [[nodiscard]] virtual std::optional<Seat> acting_seat() const = 0;
};

class HandEngine {
public:
    virtual ~HandEngine() = default;

    // Validate setup and post forced bets; return the first chance/decision node.
    [[nodiscard]] virtual std::unique_ptr<HandState> start(const HandSetup&) const = 0;
    [[nodiscard]] virtual LegalActions legal_actions(const HandState&) const = 0;
    [[nodiscard]] virtual PlayerObservation observe(const HandState&, Seat viewer) const = 0;

    // Validate before mutation. Illegal actions leave state unchanged.
    virtual void apply_action(HandState&, const Action&) const = 0;

    // Explicit one-card chance nodes allow enumeration or sampling by CFR.
    // The engine determines the recipient and street. No hidden random draws.
    [[nodiscard]] virtual std::vector<ChanceOutcome> chance_outcomes(const HandState&) const = 0;
    virtual void apply_chance(HandState&, Card) const = 0;

    // Terminal state already includes settlement. This accessor is read-only,
    // so repeated calls cannot award a pot more than once.
    [[nodiscard]] virtual HandResult result(const HandState&) const = 0;
};

} // namespace holdem
