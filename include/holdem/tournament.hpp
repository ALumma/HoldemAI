#pragma once

#include "holdem/hand_engine.hpp"

namespace holdem {

struct BlindLevel {
    Stakes stakes;
    // Deterministic simulation schedule. Wall-clock schedules can be added later.
    std::uint32_t hands = 10;
};

struct TournamentConfig {
    std::uint8_t seats = 6;
    Chips starting_stack = 10'000;
    std::vector<BlindLevel> levels;
    // Integer currency units (e.g. cents), first place first. Not poker chips.
    std::vector<std::int64_t> prizes;
};

struct TournamentStanding {
    PlayerId player;
    Chips stack;
    std::optional<std::uint32_t> finishing_place; // One-based; empty while active.
};

struct TournamentObservation {
    std::uint32_t level;
    std::uint32_t hands_until_next_level;
    std::vector<BlindLevel> blind_schedule;
    std::vector<TournamentStanding> standings;
    std::vector<std::int64_t> prizes;
};

struct PrizeAward {
    PlayerId player;
    std::int64_t amount;
};

// Initial scope: one table. The manager owns button movement and eliminations;
// the hand engine owns all rules inside a hand.
class Tournament {
public:
    virtual ~Tournament() = default;
    [[nodiscard]] virtual std::unique_ptr<Tournament> clone() const = 0;
    [[nodiscard]] virtual HandSetup next_hand() const = 0;
    // Accept each hand exactly once; validate identity and chip conservation.
    virtual void finish_hand(HandId, const HandResult&) = 0;
    [[nodiscard]] virtual TournamentObservation observe() const = 0;
    [[nodiscard]] virtual bool is_finished() const = 0;
    [[nodiscard]] virtual std::vector<PrizeAward> awards() const = 0;
};

// A concrete implementation/factory will consume TournamentConfig and player IDs.

} // namespace holdem
