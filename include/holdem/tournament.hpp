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
    Seat initial_button = 0;
};

struct TournamentStanding {
    PlayerId player;
    Chips stack;
    std::optional<std::uint32_t> finishing_place; // One-based; ties share best place.
};

struct TournamentObservation {
    std::uint32_t level;
    std::uint32_t hands_until_next_level; // Zero at final level (held indefinitely).
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

void validate(const TournamentConfig& config);

// Dead-button single-table freezeout. Input player order assigns seats 0..N-1.
// Simultaneous eliminations rank by starting stack; equal stacks split prizes.
class SingleTableTournament final : public Tournament {
public:
    SingleTableTournament(TournamentConfig config, std::vector<PlayerId> players);
    [[nodiscard]] std::unique_ptr<Tournament> clone() const override;
    [[nodiscard]] HandSetup next_hand() const override;
    void finish_hand(HandId id, const HandResult& result) override;
    [[nodiscard]] TournamentObservation observe() const override;
    [[nodiscard]] bool is_finished() const override;
    [[nodiscard]] std::vector<PrizeAward> awards() const override;

private:
    void apply_result(HandId id, const HandResult& result);
    [[nodiscard]] Seat next_live(Seat after) const;
    void assign_prizes(std::span<const std::size_t> seats, std::uint32_t place);

    TournamentConfig config_;
    std::vector<TournamentStanding> standings_;
    std::vector<std::int64_t> awarded_;
    HandId next_id_ = 1;
    std::uint32_t level_ = 0;
    std::uint32_t hands_in_level_ = 0;
    Seat button_ = 0;
    Seat small_blind_anchor_ = 0;
    Seat big_blind_ = 0;
};

} // namespace holdem
