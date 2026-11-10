#pragma once

#include "holdem/tournament.hpp"

#include <span>
#include <iosfwd>
#include <functional>
#include <map>
#include <random>
#include <utility>

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

struct PotFraction {
    std::uint32_t numerator;
    std::uint32_t denominator;
};

class PotBetAbstraction final : public ActionAbstraction {
public:
    explicit PotBetAbstraction(std::vector<PotFraction> fractions = {{1, 2}, {1, 1}},
                               bool include_all_in = true);
    [[nodiscard]] std::vector<Action> candidates(const DecisionInput&) const override;
private:
    std::vector<PotFraction> fractions_;
    bool include_all_in_;
};

class UniformPolicy final : public Policy {
public:
    [[nodiscard]] std::vector<double> probabilities(
        const DecisionInput&, std::span<const Action> candidates) const override;
};

void validate_probabilities(std::span<const double> probabilities);
[[nodiscard]] std::vector<double> regret_matching(std::span<const double> regrets);
// Explicit caller-owned RNG. Reproducible with the same seed and call sequence.
[[nodiscard]] std::size_t sample_action(std::span<const double> probabilities, std::mt19937_64& rng);

// Adapter contract for finite, perfect-recall games. Player indexes are [0, N).
// Information-set keys include all recalled observations/actions, never hidden
// opponent information. Ordered actions must agree at every occurrence of a key.
// Non-applicable methods throw by default, so simple test games can stay small.
class CfrState {
public:
    virtual ~CfrState() = default;
    [[nodiscard]] virtual NodeKind node_kind() const = 0;
    [[nodiscard]] virtual std::size_t current_player() const;
    [[nodiscard]] virtual std::string information_set() const;
    [[nodiscard]] virtual std::vector<Action> actions() const;
    [[nodiscard]] virtual std::unique_ptr<CfrState> child(const Action&) const;
    [[nodiscard]] virtual std::vector<double> chance_probabilities() const;
    [[nodiscard]] virtual std::unique_ptr<CfrState> chance_child(std::size_t outcome) const;
    [[nodiscard]] virtual std::vector<double> utilities() const;
};

using InformationSet = std::pair<std::size_t, std::string>;
struct CfrEntry {
    std::vector<Action> actions;
    std::vector<double> cumulative_regrets;
    std::vector<double> strategy_sum;
};

// Full-tree simultaneous-update vanilla CFR, intended as a correctness baseline
// for small games. Not a scalable full-Hold'em or neural trainer. General-player
// utilities are supported; the usual equilibrium guarantee is two-player zero-sum.
class TabularCfr {
public:
    explicit TabularCfr(std::size_t players, std::size_t max_depth = 256);
    void train(const CfrState& root, std::uint64_t iterations = 1);
    [[nodiscard]] std::vector<double> average_strategy(const InformationSet& key) const;
    [[nodiscard]] const std::map<InformationSet, CfrEntry>& entries() const { return entries_; }
    [[nodiscard]] std::uint64_t iterations() const { return iterations_; }
    void save(std::ostream&) const;
    void load(std::istream&); // Transactional; rejects incompatible player count.
private:
    std::size_t players_;
    std::size_t max_depth_;
    std::uint64_t iterations_ = 0;
    std::map<InformationSet, CfrEntry> entries_;
};

// A trained strategy exposed through the same public-input Policy interface.
// The encoder must match the game's information-set encoding. Unknown keys or
// different candidate ordering are errors rather than silent strategy changes.
class AverageStrategyPolicy final : public Policy {
public:
    using Encoder = std::function<InformationSet(const DecisionInput&)>;
    AverageStrategyPolicy(std::shared_ptr<const TabularCfr> trainer, Encoder encoder);
    [[nodiscard]] std::vector<double> probabilities(
        const DecisionInput&, std::span<const Action> candidates) const override;
private:
    std::shared_ptr<const TabularCfr> trainer_;
    Encoder encoder_;
};

} // namespace holdem
